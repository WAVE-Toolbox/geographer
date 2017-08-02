/*
 * KMeans.h
 *
 *  Created on: 19.07.2017
 *      Author: moritz
 */

#pragma once

#include <vector>
#include <scai/lama/DenseVector.hpp>
#include <scai/tracing.hpp>

#include "quadtree/QuadNodeCartesianEuclid.h"

using scai::lama::DenseVector;

namespace ITI {
namespace KMeans {

template<typename IndexType, typename ValueType>
DenseVector<IndexType> computePartition(const std::vector<DenseVector<ValueType>> &coordinates, IndexType k, const DenseVector<IndexType> &nodeWeights,
		const std::vector<IndexType> &blockSizes, const ValueType epsilon = 0.05);

template<typename IndexType, typename ValueType>
std::vector<std::vector<ValueType> > findInitialCenters(const std::vector<DenseVector<ValueType>> &coordinates, IndexType k, const DenseVector<IndexType> &nodeWeights);

template<typename IndexType, typename ValueType>
std::vector<std::vector<ValueType> > findCenters(const std::vector<DenseVector<ValueType>> &coordinates, const DenseVector<IndexType> &partition, const IndexType k,
		const DenseVector<IndexType> &nodeWeights);

template<typename IndexType, typename ValueType>
DenseVector<IndexType> assignBlocks(const std::vector<DenseVector<ValueType> >& coordinates,
		const std::vector<std::vector<ValueType> >& centers);

template<typename IndexType, typename ValueType>
DenseVector<IndexType> assignBlocks(const std::vector<std::vector<ValueType>> &coordinates, const std::vector<std::vector<ValueType> > &centers,
		const DenseVector<IndexType> &nodeWeights, const DenseVector<IndexType> &previousAssignment,
		const std::vector<IndexType> &blockSizes,  const SpatialCell &boundingBox, const ValueType epsilon,
		std::vector<ValueType> &upperBoundOwnCenter, std::vector<ValueType> &lowerBoundNextCenter,
		std::vector<ValueType> &upperBoundNextCenter, std::vector<ValueType> &influence);

template<typename ValueType>
ValueType biggestDelta(const std::vector<std::vector<ValueType>> &firstCoords, const std::vector<std::vector<ValueType>> &secondCoords);

/**
 * Implementations
 */


template<typename IndexType, typename ValueType>
DenseVector<IndexType> computePartition(const std::vector<DenseVector<ValueType>> &coordinates, IndexType k, const DenseVector<IndexType> &nodeWeights,
		const std::vector<IndexType> &blockSizes, const ValueType epsilon) {
	SCAI_REGION( "KMeans.computePartition" );

	std::vector<std::vector<ValueType> > centers = findInitialCenters(coordinates, k, nodeWeights);
	DenseVector<IndexType> result;
	std::vector<ValueType> influence(k,1);
	const IndexType dim = coordinates.size();
	const IndexType localN = nodeWeights.getLocalValues().size();
	scai::dmemo::CommunicatorPtr comm = scai::dmemo::Communicator::getCommunicatorPtr();

	std::vector<ValueType> minCoords(dim);
	std::vector<ValueType> maxCoords(dim);
	std::vector<std::vector<ValueType> > convertedCoords(dim);
	for (IndexType d = 0; d < dim; d++) {
		scai::hmemo::ReadAccess<ValueType> rAccess(coordinates[d].getLocalValues());
		assert(rAccess.size() == localN);
		convertedCoords[d] = std::vector<ValueType>(rAccess.get(), rAccess.get()+localN);
		assert(convertedCoords[d].size() == localN);
		minCoords[d] = *std::min_element(convertedCoords[d].begin(), convertedCoords[d].end());
		maxCoords[d] = *std::max_element(convertedCoords[d].begin(), convertedCoords[d].end());
	}

	QuadNodeCartesianEuclid boundingBox(minCoords, maxCoords);
	std::cout << "Process " << comm->getRank() << ": ( ";
	for (auto coord : minCoords) std::cout << coord << " ";
	std::cout << ") , ( ";
	for (auto coord : maxCoords) std::cout << coord << " ";
	std::cout << ")" << std::endl;

	result = assignBlocks<IndexType, ValueType>(coordinates, centers);
	std::vector<ValueType> upperBoundOwnCenter(localN, std::numeric_limits<ValueType>::max());
	std::vector<ValueType> lowerBoundNextCenter(localN, 0);
	std::vector<ValueType> upperBoundNextCenter(localN, std::numeric_limits<ValueType>::max());

	IndexType i = 0;
	ValueType delta = 0;
	ValueType threshold = 2;
	do {
		result = assignBlocks(convertedCoords, centers, nodeWeights, result, blockSizes, boundingBox, epsilon, upperBoundOwnCenter, lowerBoundNextCenter, upperBoundNextCenter, influence);
		scai::hmemo::ReadAccess<IndexType> rResult(result.getLocalValues());

		std::vector<std::vector<ValueType> > newCenters = findCenters(coordinates, result, k, nodeWeights);
		std::vector<ValueType> squaredDeltas(k,0);
		std::vector<ValueType> deltas(k,0);
		for (IndexType j = 0; j < k; j++) {
			for (int d = 0; d < dim; d++) {
				ValueType diff = (centers[d][j] - newCenters[d][j]);
				squaredDeltas[j] +=  diff*diff;
			}
			deltas[j] = std::sqrt(squaredDeltas[j]);
		}

		delta = *std::max_element(deltas.begin(), deltas.end());
		const double deltaSq = delta*delta;
		double maxInfluence = *std::max_element(influence.begin(), influence.end());
		double minInfluence = *std::min_element(influence.begin(), influence.end());

		{
			SCAI_REGION( "KMeans.computePartition.updateBounds" );
			for (IndexType i = 0; i < localN; i++) {
				IndexType cluster = rResult[i];
				upperBoundOwnCenter[i] += (2*deltas[cluster]*std::sqrt(upperBoundOwnCenter[i]/influence[cluster]) + squaredDeltas[cluster])*(influence[cluster] + 1e-10);
				ValueType pureSqrt(std::sqrt(lowerBoundNextCenter[i]/maxInfluence));
				if (pureSqrt < delta) {
					lowerBoundNextCenter[i] = 0;
				} else {
					ValueType diff = (-2*delta*pureSqrt + deltaSq)*(maxInfluence + 1e-10);
					assert(diff < 0);
					lowerBoundNextCenter[i] += diff;
					if (!(lowerBoundNextCenter[i] > 0)) lowerBoundNextCenter[i] = 0;
				}
				assert(std::isfinite(lowerBoundNextCenter[i]));
			}
		}
		centers = newCenters;

		if (comm->getRank() == 0) {
			std::cout << "i: " << i << ", delta: " << delta << std::endl;
		}
		i++;
	} while (i < 50 && delta > threshold);
	return result;
}
}
} /* namespace ITI */