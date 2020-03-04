/*
 * KMeans.cpp
 *
 *  Created on: 19.07.2017
 *      Author: moritz
 */

#include <set>
#include <cmath>
#include <assert.h>
#include <algorithm>

#include <scai/dmemo/NoDistribution.hpp>
#include <scai/dmemo/GenBlockDistribution.hpp>

#include "KMeans.h"
#include "HilbertCurve.h"
#include "MultiLevel.h"
#include "quadtree/QuadNodeCartesianEuclid.h"
// temporary, for debugging
#include "FileIO.h"

namespace ITI {

template<typename ValueType>
using point = typename std::vector<ValueType>;

// base implementation
template<typename IndexType, typename ValueType>
std::vector<std::vector<point<ValueType>>> KMeans<IndexType,ValueType>::findInitialCentersSFC(
     const std::vector<DenseVector<ValueType>>& coordinates,
     const std::vector<ValueType> &minCoords,
     const std::vector<ValueType> &maxCoords,
     const scai::lama::DenseVector<IndexType> &partition,
     const std::vector<cNode<IndexType,ValueType>> hierLevel,
Settings settings) {

    SCAI_REGION("KMeans.findInitialCentersSFC");
    const IndexType localN = coordinates[0].getLocalValues().size();
    const IndexType globalN = coordinates[0].size();
    const IndexType dimensions = settings.dimensions;
    // global communicator
    const scai::dmemo::CommunicatorPtr comm = coordinates[0].getDistributionPtr()->getCommunicatorPtr();

    // the input is already partitioned into numOldBlocks number of blocks
    // for every old block we must find a number of new centers/blocks

    const std::vector<unsigned int> numNewBlocksPerOldBlock = CommTree<IndexType,ValueType>().getGrouping(hierLevel);
    const unsigned int numOldBlocks = numNewBlocksPerOldBlock.size();

    // convert coordinates, switch inner and outer order
    std::vector<std::vector<ValueType> > convertedCoords(localN, std::vector<ValueType> (dimensions,0.0));

    for (IndexType d = 0; d < dimensions; d++) {
        scai::hmemo::ReadAccess<ValueType> rAccess(coordinates[d].getLocalValues());
        assert(rAccess.size() == localN);
        for (IndexType i = 0; i < localN; i++) {
            convertedCoords[i][d] = rAccess[i];
        }
    }

    // TODO: In the hierarchical case, we need to compute new centers
    // many times, not just once. Take the hilbert indices once
    // outside the function and not every time that is called

    // the local points but sorted according to the SFC
    // needed to find the correct(based on the sfc ordering) center index
    std::vector<IndexType> sortedLocalIndices(localN);
    {
        // get local hilbert indices
        std::vector<double> sfcIndices = HilbertCurve<IndexType, ValueType>::getHilbertIndexVector(coordinates, settings.sfcResolution, settings.dimensions);
        SCAI_ASSERT_EQ_ERROR(sfcIndices.size(), localN, "wrong local number of indices (?) ");

        // prepare indices for sorting
        std::iota(sortedLocalIndices.begin(), sortedLocalIndices.end(), 0);

        // sort local indices according to SFC
        std::sort(sortedLocalIndices.begin(), sortedLocalIndices.end(), [&sfcIndices](IndexType a, IndexType b) {
            return sfcIndices[a] < sfcIndices[b];
        });
    }

    // get prefix sum for every known block

    const unsigned int numPEs = comm->getSize();
    const IndexType rootPE = 0; // set PE 0 as root

    // the global number of points of each old block
    std::vector<IndexType> globalBlockSizes(numOldBlocks);
    // global prefix sum vector of size (p+1)*numOldBlocks
    // ATTENTION: this a a concatenation of prefix sum arrays
    // the real prefix sums are [prefixSumArray[0]:prefixSumArray[numPEs]],
    // prefixSumArray[numPEs+1]:prefixSumArray[2*numPEs]], ... ,
    // example: [0,4,10,15, 0,7,15,22, 0,12,20,30, 0, ... ]
    //           block 1    block 2     block 3 ... block numOldBlocks
    // every "subarray" has size numPEs+1 (in this example numPEs=3)
    std::vector<IndexType> concatPrefixSumArray;

    {
        std::vector<IndexType> oldBlockSizes(numOldBlocks, 0);
        scai::hmemo::ReadAccess<IndexType> localPart = partition.getLocalValues();
        SCAI_ASSERT_EQ_ERROR(localPart.size(), localN, "Partition size mismatch");

        // count the size (the number of points) of every block locally
        for (unsigned int i=0; i<localN; i++) {
            IndexType thisPointBlock = localPart[i];
            oldBlockSizes[ thisPointBlock ]++;
        }

        // gather all block sizes to root
        IndexType arraySize=1;
        if (comm->getRank()==rootPE) {
            // a possible bottleneck: in the last step arraySize=k*p
            // TODO?: cut to smaller chunks and do it in rounds?
            arraySize = numPEs*numOldBlocks;
        }
        IndexType allOldBlockSizes[arraySize];
        comm->gather(allOldBlockSizes, numOldBlocks, rootPE, oldBlockSizes.data());
        std::vector<IndexType> allOldSizesVec(allOldBlockSizes, allOldBlockSizes + arraySize);
        if (comm->getRank()==rootPE) {
            SCAI_ASSERT_EQ_ERROR(globalN, std::accumulate(allOldSizesVec.begin(), allOldSizesVec.end(), 0), "Mismatch in gathered array for sizes of all blocks for PE " << *comm);
        }

        // only root PE calculates the prefixSum
        if (comm->getRank()==rootPE) {
            for (unsigned int blockId=0; blockId<numOldBlocks; blockId++) {
                // prefix sum for every block starts with 0
                concatPrefixSumArray.push_back(0);
                for (unsigned int pe=0; pe<numPEs; pe++) {
                    concatPrefixSumArray.push_back(concatPrefixSumArray.back() + allOldBlockSizes[pe*numOldBlocks+blockId]);
                }
            }
            SCAI_ASSERT_EQ_ERROR(concatPrefixSumArray.size(), (numPEs+1)*numOldBlocks, "Prefix sum array has wrong size");
        } else {
            concatPrefixSumArray.resize((numPEs+1)*numOldBlocks, 0);
        }

        comm->bcast(concatPrefixSumArray.data(),(numPEs+1)*numOldBlocks, rootPE);

        for (unsigned int b=0; b<numOldBlocks; b++) {
            globalBlockSizes[b] = concatPrefixSumArray[(b+1)*numPEs+b];
            SCAI_ASSERT_EQ_ERROR(concatPrefixSumArray[b*(numPEs+1)], 0, "Wrong concat prefix sum array, values at indices b*(numPEs+1) must be zero, Failed for b=" << b);
        }
        IndexType prefixSumCheckSum = std::accumulate(globalBlockSizes.begin(), globalBlockSizes.end(), 0);
        SCAI_ASSERT_EQ_ERROR(prefixSumCheckSum, globalN, "Global sizes mismatch. Wrong calculation of prefix sum?");
    }

    // compute wanted indices for initial centers
    // newCenterIndWithinBLock[i] = a vector with the indices of the
    // centers for block i
    // newCenterIndWithinBLock[i].size() = numNewBlocksPerOldBlock[b], i.e., the
    // new number of blocks to partition previous block i
    // ATTENTION: newCenterIndWithinBLock[i][j] = x: is the index of the
    // center within block i. If x is 30, then we want the 30-th point
    // of block i.

    std::vector<std::vector<IndexType>> newCenterIndWithinBLock(numOldBlocks);

    // for all old blocks
    for (IndexType b=0; b<numOldBlocks; b++) {
        // the number of centers for block b
        IndexType k_b = numNewBlocksPerOldBlock[b];
        newCenterIndWithinBLock[b].resize(k_b);
        for (IndexType i = 0; i < k_b; i++) {
            // wantedIndices[i] = i * (globalN / k) + (globalN / k)/2;
            newCenterIndWithinBLock[b][i] = i*(globalBlockSizes[b]/k_b) + (globalBlockSizes[b]/k_b)/2;
        }
    }

    const IndexType thisPE = comm->getRank();

    // the centers to be returned, each PE fills only with owned centers
    std::vector<std::vector<point<ValueType>>> centersPerNewBlock(numOldBlocks);
    for (IndexType b=0; b<numOldBlocks; b++) {
        centersPerNewBlock[b].resize(numNewBlocksPerOldBlock[b], point<ValueType>(dimensions, 0.0));
    }

    // for debugging
    IndexType sumOfRanges = 0;
    IndexType numOwnedCenters = 0;

    for (IndexType b=0; b<numOldBlocks; b++) {
        IndexType fromInd = b*(numPEs+1)+thisPE;
        assert(fromInd+1<concatPrefixSumArray.size());

        // the range of the indices for block b for this PE
        IndexType rangeStart = concatPrefixSumArray[ fromInd ];
        IndexType rangeEnd = concatPrefixSumArray[ fromInd+1];
        sumOfRanges += rangeEnd-rangeStart;
        // keep a counter that indicates the index of a point within
        // a block in this PE.
        IndexType counter = rangeStart;

        // center indices for block b, pass by reference so not to copy
        const std::vector<IndexType>& centersForThisBlock = newCenterIndWithinBLock[b];

        // TODO: optimize? Now, complexity is localN*number of owned centers
        // can we do it with one linear scan? Well, newCenterIndWithinBLock[b] is expected
        // to be kinf of small unless k>>p

        // if some center indexes are local in this PE, store them.
        // Later, we will scan the local points for their coordinates
        for (unsigned int j=0; j<centersForThisBlock.size(); j++) {
            IndexType centerInd = centersForThisBlock[j];
            counter = rangeStart;// reset counter for next center
            // if center index for block b is owned by thisPE
            if (centerInd>=rangeStart and centerInd<=rangeEnd) {

                // since we own a center, go over all local points
                // and calculate their within-block index for the block
                // they belong to
                scai::hmemo::ReadAccess<IndexType> localPart = partition.getLocalValues();
                for (unsigned int i=0; i<localN; i++) {
                    // consider points based on their sorted sfc index
                    IndexType sortedIndex = sortedLocalIndices[i];
                    IndexType thisPointBlock = localPart[ sortedIndex ];
                    // TODO: remove assertion?
                    assert(thisPointBlock<numOldBlocks);
                    if (thisPointBlock!=b) {
                        continue;// not in desired block
                    }

                    IndexType withinBlockIndex = counter;
                    // desired center found
                    if (withinBlockIndex==centerInd) {
                        // store center coords
                        centersPerNewBlock[b][j] = convertedCoords[ sortedIndex ];
                        numOwnedCenters++;
                        // PRINT(*comm <<": adding center "<< centerInd << " with coordinates " << convertedCoords[sortedIndex][0] << ", " << convertedCoords[sortedIndex][1]);
                        break;
                    }
                    counter++;
                }// for i<localN
                SCAI_ASSERT_LE_ERROR(counter, rangeEnd, "Within-block index out of bounds");
            }// if center is local
        }// for j<centersForThisBlock.size()
    }// for b<numOldBlocks

    SCAI_ASSERT_EQ_ERROR(sumOfRanges, localN, thisPE << ": Sum of owned number of points per block should be equal the total number of local points");

    if (settings.debugMode) {
        PRINT(*comm << ": owns " << numOwnedCenters << " centers");
        unsigned int numNewTotalBlocks = std::accumulate(numNewBlocksPerOldBlock.begin(), numNewBlocksPerOldBlock.end(), 0);
        SCAI_ASSERT_EQ_ERROR(comm->sum(numOwnedCenters), numNewTotalBlocks, "Not all centers were found");
    }

    //
    // global sum operation. Doing it in a separate loop on purpose
    // since different PEs own centers from different blocks and a lot of
    // blocks own no centers at all
    //

    for (IndexType b=0; b<numOldBlocks; b++) {

        SCAI_ASSERT_EQ_ERROR(centersPerNewBlock[b][0].size(), dimensions, "Dimension mismatch for center");
        IndexType numCenters = centersPerNewBlock[b].size();

        // pack in a raw array
        std::vector<ValueType> allCenters(numCenters*dimensions);

        for (unsigned int c=0; c<numCenters; c++) {
            const point<ValueType>& thisCenter = centersPerNewBlock[b][c];
            // copy this center
            std::copy(thisCenter.begin(), thisCenter.end(), allCenters.begin() +c*dimensions);
        }

        // global sum
        comm->sumImpl(allCenters.data(), allCenters.data(), numCenters*dimensions, scai::common::TypeTraits<ValueType>::stype);

        // unpack back to vector<point>
        for (unsigned int c=0; c<numCenters; c++) {
            for (IndexType d=0; d<dimensions; d++) {
                // center c, for block b
                centersPerNewBlock[b][c][d] = allCenters[ c*dimensions+d ];
            }
        }
    }

    return centersPerNewBlock;
}


// overloaded function for non-hierarchical version.
// Set partition to 0 for all points
// A "flat" communication tree
// and return only the first (there is only one) group of centers
template<typename IndexType, typename ValueType>
std::vector<std::vector<ValueType>>  KMeans<IndexType,ValueType>::findInitialCentersSFC(
     const std::vector<DenseVector<ValueType>>& coordinates,
     const std::vector<ValueType> &minCoords,
     const std::vector<ValueType> &maxCoords,
Settings settings) {

    // TODO: probably must also change the settings.numBlocks

    // homogeneous case, all PEs have the same memory and speed
    // there is only hierarchy level
    // TODO: probably this needs some further adaptation for the hierarchical version

    // TODO: use commTree::createFlat?
    std::vector<cNode<IndexType,ValueType>> leaves(settings.numBlocks);
    for (int i=0; i<settings.numBlocks; i++) {
        leaves[i] = cNode<IndexType,ValueType>(std::vector<unsigned int> {0}, {1.0});
    }

    // every point belongs to one block in the beginning
    scai::lama::DenseVector<IndexType> partition(coordinates[0].getDistributionPtr(), 0);

    // return a vector of size 1 with
    std::vector<std::vector<point<ValueType>>> initialCenters = findInitialCentersSFC(coordinates, minCoords, maxCoords, partition, leaves, settings);

    SCAI_ASSERT_EQ_ERROR(initialCenters.size(), 1, "Wrong vector size");
    SCAI_ASSERT_EQ_ERROR(initialCenters[0].size(), settings.numBlocks, "Wrong vector size");

    // TODO: must change convert centers to a vector of size=dimensions
    // where initialCenters[0][d][i] is the d-th coordinate of the i-th center
    IndexType dimensions = settings.dimensions;

    SCAI_ASSERT_EQ_ERROR(minCoords.size(), settings.dimensions, "Wrong center dimensions");

// TODO: check/verify if we do not need to revert the vector order
    // reverse vector order here
    std::vector<std::vector<ValueType>> reversedCenters(dimensions, std::vector<ValueType>(settings.numBlocks, 0.0));
    for (unsigned int c=0; c<settings.numBlocks; c++) {
        for (unsigned int d=0; d<dimensions; d++) {
            reversedCenters[d][c] = initialCenters[0][c][d];
        }
    }
    // return reversedCenters;

    return initialCenters[0];
}


template<typename IndexType, typename ValueType>
std::vector<std::vector<ValueType>> KMeans<IndexType,ValueType>::findInitialCentersFromSFCOnly(
    const std::vector<ValueType> &minCoords,
    const std::vector<ValueType> &maxCoords,
    Settings settings) {
    // This assumes that minCoords is 0!
    // TODO: change or remove
    const IndexType dimensions = settings.dimensions;
    const IndexType k = settings.numBlocks;

    // set local values in vector, leave non-local values with zero
    std::vector<std::vector<ValueType> > result(dimensions);
    for (IndexType d = 0; d < dimensions; d++) {
        result[d].resize(k);
    }

    ValueType offset = 1.0/(ValueType(k)*2.0);
    std::vector<ValueType> centerCoords(dimensions,0);
    for (IndexType i = 0; i < k; i++) {
        ValueType centerHilbInd = i/ValueType(k) + offset;
        centerCoords = HilbertCurve<IndexType,ValueType>::HilbertIndex2Point(centerHilbInd, settings.sfcResolution, settings.dimensions);
        SCAI_ASSERT_EQ_ERROR(centerCoords.size(), dimensions, "Wrong dimensions for center.");

        // centerCoords are points in the unit square; project back to input space
        for (IndexType d = 0; d < dimensions; d++) {
            result[d][i] = (centerCoords[d]*(maxCoords[d]-minCoords[d]))+minCoords[d];
        }
    }
    return result;
}

//TODO: how to treat multiple weights
template<typename IndexType, typename ValueType>
std::vector<std::vector<ValueType>> KMeans<IndexType,ValueType>::findLocalCenters(const std::vector<DenseVector<ValueType> >& coordinates, const DenseVector<ValueType> &nodeWeights) {

    const IndexType dim = coordinates.size();
    const IndexType localN = coordinates[0].getLocalValues().size();

    // get sum of local weights

    scai::hmemo::ReadAccess<ValueType> rWeights(nodeWeights.getLocalValues());
    SCAI_ASSERT_EQ_ERROR(rWeights.size(), localN, "Mismatch of nodeWeights and coordinates size. Check distributions.");

    ValueType localWeightSum = 0;
    for (IndexType i=0; i<localN; i++) {
        localWeightSum += rWeights[i];
    }

    std::vector<ValueType> localCenter(dim, 0);

    for (IndexType d = 0; d < dim; d++) {
        scai::hmemo::ReadAccess<ValueType> rCoords(coordinates[d].getLocalValues());
        for (IndexType i=0; i<localN; i++) {
            // this is more expensive than summing first and dividing later, but avoids overflows
            localCenter[d] += rWeights[i]*rCoords[i]/localWeightSum;
        }
    }

    // vector of size k for every center. Each PE only stores its local center and sum center
    // so all centers are replicated in all PEs

    const scai::dmemo::CommunicatorPtr comm = coordinates[0].getDistribution().getCommunicatorPtr();
    const IndexType numPEs = comm->getSize();
    const IndexType thisPE = comm->getRank();
    std::vector<std::vector<ValueType> > result(dim, std::vector<ValueType>(numPEs,0));
    for (IndexType d=0; d<dim; d++) {
        result[d][thisPE] = localCenter[d];
    }

    for (IndexType d=0; d<dim; d++) {
        comm->sumImpl(result[d].data(), result[d].data(), numPEs, scai::common::TypeTraits<ValueType>::stype);
    }
    return result;
}


//TODO: how to treat multiple weights
template<typename IndexType, typename ValueType>
template<typename Iterator>
std::vector<point<ValueType>> KMeans<IndexType,ValueType>::findCenters(
    const std::vector<DenseVector<ValueType>>& coordinates,
    const DenseVector<IndexType>& partition,
    const IndexType k,
    const Iterator firstIndex,
    const Iterator lastIndex,
    const DenseVector<ValueType>& nodeWeights) {
    SCAI_REGION("KMeans.findCenters");

    const IndexType dim = coordinates.size();
    const scai::dmemo::DistributionPtr resultDist(new scai::dmemo::NoDistribution(k));
    const scai::dmemo::CommunicatorPtr comm = partition.getDistribution().getCommunicatorPtr();

    // TODO: check that distributions align

    std::vector<std::vector<ValueType> > result(dim);
    std::vector<ValueType> weightSum(k, 0.0);

    scai::hmemo::ReadAccess<ValueType> rWeights(nodeWeights.getLocalValues());
    scai::hmemo::ReadAccess<IndexType> rPartition(partition.getLocalValues());

    // compute weight sums
    for (Iterator it = firstIndex; it != lastIndex; it++) {
        const IndexType i = *it;
        const IndexType part = rPartition[i];
        const ValueType weight = rWeights[i];
        weightSum[part] += weight;
        // the lines above are equivalent to: weightSum[rPartition[*it]] += rWeights[*it];
    }

    // find local centers
    for (IndexType d = 0; d < dim; d++) {
        result[d].resize(k,0);
        scai::hmemo::ReadAccess<ValueType> rCoords(coordinates[d].getLocalValues());

        for (Iterator it = firstIndex; it != lastIndex; it++) {
            const IndexType i = *it;
            const IndexType part = rPartition[i];
            result[d][part] += rCoords[i]*rWeights[i] / weightSum[part];// this is more expensive than summing first and dividing later, but avoids overflows
        }
    }

    // communicate local centers and weight sums
    std::vector<ValueType> totalWeight(k, 0);
    comm->sumImpl(totalWeight.data(), weightSum.data(), k, scai::common::TypeTraits<ValueType>::stype);

    // compute updated centers as weighted average
    for (IndexType d = 0; d < dim; d++) {
        for (IndexType j = 0; j < k; j++) {
            ValueType weightRatio = (ValueType(weightSum[j]) / totalWeight[j]);

            ValueType weightedCoord = weightSum[j] == 0 ? 0 : result[d][j] * weightRatio;
            result[d][j] = weightedCoord;
            assert(std::isfinite(result[d][j]));

            // make empty clusters explicit
            if (totalWeight[j] == 0) {
                result[d][j] = NAN;
            }
        }

        comm->sumImpl(result[d].data(), result[d].data(), k, scai::common::TypeTraits<ValueType>::stype);
    }

    return result;
}


template<typename IndexType, typename ValueType>
std::vector<point<ValueType>> KMeans<IndexType,ValueType>::vectorTranspose(const std::vector<std::vector<ValueType>>& points) {
    const IndexType dim = points.size();
    SCAI_ASSERT_GT_ERROR(dim, 0, "Dimension of points cannot be 0");

    const IndexType numPoints = points[0].size();
    SCAI_ASSERT_GT_ERROR(numPoints, 0, "Empty vector of points");

    std::vector<point<ValueType>> retPoints(numPoints, point<ValueType>(dim));

    for (unsigned int d=0; d<dim; d++) {
        for (unsigned int i=0; i<numPoints; i++) {
            retPoints[i][d] = points[d][i];
        }
    }

    return retPoints;
}

template<typename IndexType, typename ValueType>
template<typename Iterator>
DenseVector<IndexType> KMeans<IndexType,ValueType>::assignBlocks(
    const std::vector<std::vector<ValueType>>& coordinates,
    const std::vector<point<ValueType>>& centers,
    const std::vector<IndexType>& blockSizesPrefixSum,
    const Iterator firstIndex,
    const Iterator lastIndex,
    const std::vector<std::vector<ValueType>> &nodeWeights,
    const std::vector<std::vector<ValueType>> &normalizedNodeWeights,
    const DenseVector<IndexType> &previousAssignment,
    const DenseVector<IndexType> &oldBlock, // if repartition, this is the partition to be rebalanced
    const std::vector<std::vector<ValueType>> &targetBlockWeights,
    const SpatialCell<ValueType> &boundingBox,
    std::vector<ValueType> &upperBoundOwnCenter,
    std::vector<ValueType> &lowerBoundNextCenter,
    std::vector<std::vector<ValueType>> &influence,
    std::vector<ValueType> &imbalance,
    Settings settings,
    Metrics<ValueType>& metrics) {
    SCAI_REGION("KMeans.assignBlocks");

    const IndexType dim = coordinates.size();
    const scai::dmemo::DistributionPtr dist = previousAssignment.getDistributionPtr();
    const scai::dmemo::CommunicatorPtr comm = dist->getCommunicatorPtr();
    const IndexType localN = dist->getLocalSize();
    const IndexType currentLocalN = std::distance(firstIndex, lastIndex);

    if (currentLocalN < 0) {
        throw std::runtime_error("currentLocalN: " + std::to_string(currentLocalN));
    }

    if (currentLocalN == 0) {
        PRINT("Process " + std::to_string(comm->getRank()) + " has no local points!");
        return previousAssignment;
    }

    // if repartition, numOldBlocks=1
    // number of blocks from the previous hierarchy
    const IndexType numOldBlocks= blockSizesPrefixSum.size()-1;

    if (settings.repartition) {
        assert(numOldBlocks==1);
        assert(blockSizesPrefixSum.size()==2);
    }
    const IndexType numNodeWeights = nodeWeights.size();

    if (settings.debugMode and not settings.repartition) {
        const IndexType maxPart = oldBlock.max(); // global operation
        SCAI_ASSERT_EQ_ERROR(numOldBlocks-1, maxPart, "The provided old assignment must have equal number of blocks as the length of the vector with the new number of blocks per part");
    }

    // numNewBlocks is equivalent to 'k' in the classic version
    IndexType numNewBlocks = centers.size();

    SCAI_ASSERT_EQ_ERROR(blockSizesPrefixSum.back(), numNewBlocks, "Total number of new blocks mismatch");

    // centers are given as a 1D vector alongside with a prefix sum vector
    const std::vector<point<ValueType>>& centers1DVector = centers;

    SCAI_ASSERT_EQ_ERROR(centers1DVector.size(), numNewBlocks, "Vector size mismatch");
    SCAI_ASSERT_EQ_ERROR(centers1DVector[0].size(), dim, "Center dimensions mismatch");
    SCAI_ASSERT_EQ_ERROR(influence.size(), numNodeWeights, "Vector size mismatch");
    for (IndexType i = 0; i < numNodeWeights; i++) {
        SCAI_ASSERT_EQ_ERROR(influence[i].size(), numNewBlocks, "Vector size mismatch");
    }

    // pre-filter possible closest blocks
    std::vector<ValueType> minDistanceAllBlocks(numNewBlocks);
    std::vector<ValueType> effectMinDistAllBlocks(numNewBlocks);

    // for all new blocks
    for (IndexType newB=0; newB<numNewBlocks; newB++) {
        SCAI_REGION("KMeans.assignBlocks.filterCenters");

        point<ValueType> center = centers1DVector[newB];
        ValueType influenceMin = std::numeric_limits<ValueType>::max();
        for (IndexType i = 0; i < numNodeWeights; i++) {
            influenceMin = std::min(influenceMin, influence[i][newB]);
        }

        minDistanceAllBlocks[newB] = boundingBox.distances(center).first;
        assert(std::isfinite(minDistanceAllBlocks[newB]));
        effectMinDistAllBlocks[newB] = minDistanceAllBlocks[newB]\
                                       *minDistanceAllBlocks[newB]\
                                       *influenceMin;
        assert(std::isfinite(effectMinDistAllBlocks[newB]));
    }

    // sort centers according to their distance from the bounding box of this PE

    std::vector<IndexType> clusterIndicesAllBlocks(numNewBlocks);
    // cluster indices are "global": from 0 to numNewBlocks
    std::iota(clusterIndicesAllBlocks.begin(), clusterIndicesAllBlocks.end(), 0);

    // remember, if repartition, numOldBlocks=1 and blockSizesPrefixSum.size()=2

    for (IndexType oldB=0; oldB<numOldBlocks; oldB++) {
        const unsigned int rangeStart = blockSizesPrefixSum[oldB];
        const unsigned int rangeEnd = blockSizesPrefixSum[oldB+1];
        typename std::vector<IndexType>::iterator startIt = clusterIndicesAllBlocks.begin()+rangeStart;
        typename std::vector<IndexType>::iterator endIt = clusterIndicesAllBlocks.begin()+rangeEnd;
        // TODO: remove not needed assertions
        SCAI_ASSERT_LT_ERROR(rangeStart, rangeEnd, "Prefix sum vectors is wrong");
        SCAI_ASSERT_LE_ERROR(rangeEnd, numNewBlocks, "Range out of bounds");

        // sort the part of the indices that belong to this old block
        std::sort(startIt, endIt,
        [&](IndexType a, IndexType b) {
            return effectMinDistAllBlocks[a] < effectMinDistAllBlocks[b] \
                   || (effectMinDistAllBlocks[a] == effectMinDistAllBlocks[b] && a < b);
        }
                 );

        std::sort(effectMinDistAllBlocks.begin()+rangeStart, effectMinDistAllBlocks.begin()+rangeEnd);
    }

    IndexType iter = 0;
    IndexType skippedLoops = 0;
    ValueType totalBalanceTime = 0;	// for timing/profiling
    std::vector<std::vector<bool>> influenceGrew(numNodeWeights, std::vector<bool>(numNewBlocks));
    std::vector<ValueType> influenceChangeUpperBound(numNewBlocks, 1+settings.influenceChangeCap);
    std::vector<ValueType> influenceChangeLowerBound(numNewBlocks, 1-settings.influenceChangeCap);

    // compute assignment and balance
    DenseVector<IndexType> assignment = previousAssignment;
    bool allWeightsBalanced = false; // balance over all weights and all blocks


    // iterate if necessary to achieve balance
    do
    {
        std::chrono::time_point<std::chrono::high_resolution_clock> balanceStart = std::chrono::high_resolution_clock::now();
        SCAI_REGION("KMeans.assignBlocks.balanceLoop");

        // the block weight for all new blocks
        std::vector<std::vector<ValueType>> blockWeights(numNodeWeights, std::vector<ValueType>(numNewBlocks, 0.0));

        std::vector<ValueType> influenceEffectOfOwn(currentLocalN, 0); // TODO: also potentially move to outer function

        IndexType totalComps = 0;
        skippedLoops = 0;
        IndexType balancedBlocks = 0;

        scai::hmemo::ReadAccess<IndexType> rOldBlock(oldBlock.getLocalValues());
        scai::hmemo::WriteAccess<IndexType> wAssignment(assignment.getLocalValues());
        {
            SCAI_REGION("KMeans.assignBlocks.balanceLoop.assign");
            // for the sampled range
            for (Iterator it = firstIndex; it != lastIndex; it++) {
                const IndexType i = *it;
                const IndexType oldCluster = wAssignment[i];
                const IndexType fatherBlock = rOldBlock[i];
                const IndexType veryLocalI = std::distance(firstIndex, it);

                if (not settings.repartition) {
                    SCAI_ASSERT_LT_ERROR(fatherBlock, numOldBlocks, "Wrong father block index");
                } else {
                    // numOldBlocks=1 but father block<numNewBlocks
                    SCAI_ASSERT_LT_ERROR(fatherBlock, numNewBlocks, "Wrong father block index");
                }

                assert(influenceEffectOfOwn[veryLocalI] == 0);
                for (IndexType j = 0; j < numNodeWeights; j++) {
                    influenceEffectOfOwn[veryLocalI] += influence[j][oldCluster]*normalizedNodeWeights[j][i];
                }

                if (lowerBoundNextCenter[i] > upperBoundOwnCenter[i]) {
                    // cluster assignment cannot have changed.
                    // wAssignment[i] = wAssignment[i];
                    skippedLoops++;
                } else {
                    ValueType sqDistToOwn = 0;
                    const point<ValueType>& myCenter = centers1DVector[oldCluster];
                    for (IndexType d = 0; d < dim; d++) {
                        sqDistToOwn += std::pow(myCenter[d]-coordinates[d][i], 2);
                    }

                    ValueType newEffectiveDistance = sqDistToOwn*influenceEffectOfOwn[veryLocalI];
                    SCAI_ASSERT_LE_ERROR(newEffectiveDistance, upperBoundOwnCenter[i], "Distance upper bound was wrong");
                    upperBoundOwnCenter[i] = newEffectiveDistance;
                    if (lowerBoundNextCenter[i] > upperBoundOwnCenter[i]) {
                        // cluster assignment cannot have changed.
                        // wAssignment[i] = wAssignment[i];
                        skippedLoops++;
                    } else {
                        // check the centers of this old block to find the closest one
                        IndexType bestBlock = 0;
                        ValueType bestValue = std::numeric_limits<ValueType>::max();
                        ValueType influenceEffectOfBestBlock = -1;
                        IndexType secondBest = 0;
                        ValueType secondBestValue = std::numeric_limits<ValueType>::max();

                        // if repartition, blockSizesPrefixSum only has two elements and the fatherBlock index is wrong
                        // where the range of indices starts for the father block
                        const IndexType rangeStart = settings.repartition ? 0 : blockSizesPrefixSum[fatherBlock];
                        const IndexType rangeEnd =  settings.repartition ? blockSizesPrefixSum.back() : blockSizesPrefixSum[fatherBlock+1];
                        SCAI_ASSERT_LE_ERROR(rangeEnd, clusterIndicesAllBlocks.size(), "Range out of bounds");

                        // start with the first center index
                        IndexType c = rangeStart;

                        // check all centers belonging to the father block to find the closest
                        while (c < rangeEnd && secondBestValue > effectMinDistAllBlocks[c]) {
                            totalComps++;
                            // remember: cluster centers are sorted according to their distance from the bounding box of this PE
                            // also, the cluster indices go from 0 till numNewBlocks
                            IndexType j = clusterIndicesAllBlocks[c];// maybe it would be useful to sort the whole centers array, aligning memory accesses.

                            // squared distance from previous assigned center
                            ValueType sqDist = 0;
                            const point<ValueType>& myCenter = centers1DVector[j];
                            // TODO: restructure arrays to align memory accesses better in inner loop
                            for (IndexType d = 0; d < dim; d++) {
                                sqDist += std::pow(myCenter[d]-coordinates[d][i], 2);
                            }

                            ValueType influenceEffect = 0;
                            for (IndexType w = 0; w < numNodeWeights; w++) {
                                influenceEffect += influence[w][j]*normalizedNodeWeights[w][i];
                            }
                            const ValueType effectiveDistance = sqDist*influenceEffect;

                            // update best and second-best centers
                            if (effectiveDistance < bestValue) {
                                secondBest = bestBlock;
                                secondBestValue = bestValue;
                                bestBlock = j;
                                bestValue = effectiveDistance;
                                influenceEffectOfBestBlock = influenceEffect;
                            } else if (effectiveDistance < secondBestValue) {
                                secondBest = j;
                                secondBestValue = effectiveDistance;
                            }
                            c++;
                        } // while

                        if (rangeEnd - rangeStart > 1) {
                            SCAI_ASSERT_NE_ERROR(bestBlock, secondBest, "Best and second best should be different");
                        }

                        assert(secondBestValue >= bestValue);

                        // this point has a new center
                        if (bestBlock != oldCluster) {
                            // assert(bestValue >= lowerBoundNextCenter[i]);
                            SCAI_ASSERT_GE_ERROR(bestValue, lowerBoundNextCenter[i], \
                                                 "PE " << comm->getRank() << ": difference " << std::abs(bestValue - lowerBoundNextCenter[i]) << \
                                                 " for i= " << i << ", oldCluster: " << oldCluster << ", newCluster: " << bestBlock << \
                                                 ", influenceEffect: " << influenceEffectOfBestBlock);
                        }

                        upperBoundOwnCenter[i] = bestValue;
                        lowerBoundNextCenter[i] = secondBestValue;
                        influenceEffectOfOwn[veryLocalI] = influenceEffectOfBestBlock;
                        wAssignment[i] = bestBlock;
                    }
                }
                // we found the best block for this point; increase the weight of this block
                for (IndexType j = 0; j <numNodeWeights; j++) {
                    blockWeights[j][wAssignment[i]] += nodeWeights[j][i];
                }

            }// for sampled indices

            std::chrono::duration<ValueType,std::ratio<1>> balanceTime = std::chrono::high_resolution_clock::now() - balanceStart;
            // timePerPE[comm->getRank()] += balanceTime.count();

            comm->synchronize();
        }// assignment block

        for (IndexType j = 0; j < numNodeWeights; j++)
        {
            SCAI_REGION("KMeans.assignBlocks.balanceLoop.blockWeightSum");
            comm->sumImpl(blockWeights[j].data(), blockWeights[j].data(), numNewBlocks, scai::common::TypeTraits<ValueType>::stype);
        }

        // calculate imbalance for every new block and every weight
        allWeightsBalanced = true;
        std::vector<std::vector<ValueType>> imbalancesPerBlock(numNodeWeights, std::vector<ValueType>(numNewBlocks));
        for (IndexType i = 0; i < numNodeWeights; i++) {
            for (IndexType newB=0; newB<numNewBlocks; newB++) {
                ValueType optWeight = targetBlockWeights[i][newB];
                imbalancesPerBlock[i][newB] = (ValueType(blockWeights[i][newB] - optWeight)/optWeight);
            }
            // imbalance for each weight is the maximum imbalance of all new blocks
            imbalance[i] = *std::max_element(imbalancesPerBlock[i].begin(), imbalancesPerBlock[i].end());

            if (settings.verbose and imbalance[i]<0) {
                PRINT0("Warning, imbalance in weight " + std::to_string(i) + " is " + std::to_string(imbalance[i]) + ". Probably the given target block sizes are all too large.");
            }

            if (imbalance[i] > settings.epsilon) {// TODO: generalize with multiple epsilons
                allWeightsBalanced = false;
            }
        }

        // adapt influence values
        ValueType minRatio = std::numeric_limits<ValueType>::max();
        ValueType maxRatio = -std::numeric_limits<ValueType>::min();
        std::vector<std::vector<ValueType>> oldInfluence = influence;// size=numNewBlocks
        for (IndexType i = 0; i < numNodeWeights; i++) {
            assert(oldInfluence[i].size()== numNewBlocks);

            for (IndexType j=0; j<numNewBlocks; j++) {
                SCAI_REGION("KMeans.assignBlocks.balanceLoop.influence");

                ValueType ratio = ValueType(blockWeights[i][j])/targetBlockWeights[i][j];
                if (std::abs(ratio - 1) < settings.epsilon) {
                    balancedBlocks++; // TODO: update for multiple weights
                    if (settings.freezeBalancedInfluence) {
                        if (1 < minRatio) minRatio = 1;
                        if (1 > maxRatio) maxRatio = 1;
                        continue;
                    }
                }
                const ValueType thisInfluence = influence[i][j];
                influence[i][j] = std::max( thisInfluence*influenceChangeLowerBound[j],
                    std::min( ValueType(thisInfluence*std::pow(ratio, settings.influenceExponent)),
                    thisInfluence*influenceChangeUpperBound[j] ) 
                    );

                assert(influence[i][j] > 0);

                ValueType influenceRatio = influence[i][j] / oldInfluence[i][j];

                assert(influenceRatio <= influenceChangeUpperBound[j] + 1e-6);
                assert(influenceRatio >= influenceChangeLowerBound[j] - 1e-6);
                if (influenceRatio < minRatio) minRatio = influenceRatio;
                if (influenceRatio > maxRatio) maxRatio = influenceRatio;

                if (settings.tightenBounds && iter > 0 && (static_cast<bool>(ratio > 1) != influenceGrew[i][j])) {
                    // influence change switched direction
                    influenceChangeUpperBound[j] = 0.1 + 0.9*influenceChangeUpperBound[j];
                    influenceChangeLowerBound[j] = 0.1 + 0.9*influenceChangeLowerBound[j];
                    assert(influenceChangeUpperBound[j] > 1);
                    assert(influenceChangeLowerBound[j] < 1);
                }
                influenceGrew[i][j] = static_cast<bool>(ratio > 1);
            }// for numNewBlocks
        }// for numNodeWeights

        // update bounds
        {
            SCAI_REGION("KMeans.assignBlocks.balanceLoop.updateBounds");
            for (Iterator it = firstIndex; it != lastIndex; it++) {
                const IndexType i = *it;
                const IndexType cluster = wAssignment[i];
                const IndexType veryLocalI = std::distance(firstIndex, it);// maybe optimize?
                ValueType newInfluenceEffect = 0;
                for (IndexType j = 0; j < numNodeWeights; j++) {
                    newInfluenceEffect += influence[j][cluster]*normalizedNodeWeights[j][i];
                }

                SCAI_ASSERT_LE_ERROR((newInfluenceEffect / influenceEffectOfOwn[veryLocalI]), maxRatio + 1e-5, "Error in calculation of influence effect");
                SCAI_ASSERT_GE_ERROR((newInfluenceEffect / influenceEffectOfOwn[veryLocalI]), minRatio - 1e-5, "Error in calculation of influence effect");

                upperBoundOwnCenter[i] *= (newInfluenceEffect / influenceEffectOfOwn[veryLocalI]) + 1e-5;
                lowerBoundNextCenter[i] *= minRatio - 1e-5;
            }
        }

        // update possible closest centers
        {
            // TODO: repair once I have the equations figured out for the new boundaries
            SCAI_REGION("KMeans.assignBlocks.balanceLoop.filterCenters");
            for (IndexType newB=0; newB<numNewBlocks; newB++) {
                ValueType influenceMin = std::numeric_limits<ValueType>::max();
                for (IndexType i = 0; i < numNodeWeights; i++) {
                    influenceMin = std::min(influenceMin, influence[i][newB]);
                }

                effectMinDistAllBlocks[newB] = minDistanceAllBlocks[newB]*minDistanceAllBlocks[newB]*influenceMin;
            }

            // TODO: duplicated code as in the beginning of the assignBlocks, maybe move into lambda?
            for (IndexType oldB=0; oldB<numOldBlocks; oldB++) {
                const unsigned int rangeStart = blockSizesPrefixSum[oldB];
                const unsigned int rangeEnd = blockSizesPrefixSum[oldB+1];
                typename std::vector<IndexType>::iterator startIt = clusterIndicesAllBlocks.begin()+rangeStart;
                typename std::vector<IndexType>::iterator endIt = clusterIndicesAllBlocks.begin()+rangeEnd;
                // TODO: remove not needed assertions
                SCAI_ASSERT_LT_ERROR(rangeStart, rangeEnd, "Prefix sum vectos is wrong");
                SCAI_ASSERT_LE_ERROR(rangeEnd, numNewBlocks, "Range out of bounds");
                // SCAI_ASSERT_ERROR(endIt!=clusterIndicesAllBlocks.end(), "Iterator out of bounds");

                // sort the part of the indices that belong to this old block
                std::sort(startIt, endIt,
                [&](IndexType a, IndexType b) {
                    return effectMinDistAllBlocks[a] < effectMinDistAllBlocks[b] \
                           || (effectMinDistAllBlocks[a] == effectMinDistAllBlocks[b] && a < b);
                }
                         );
                // sort also this part of the distances
                std::sort(effectMinDistAllBlocks.begin()+rangeStart, effectMinDistAllBlocks.begin()+rangeEnd);
            }
        }

        iter++;

        if (settings.verbose) {
            const IndexType takenLoops = currentLocalN - skippedLoops;
            const ValueType averageComps = ValueType(totalComps) / currentLocalN;
            std::vector<ValueType> influenceSpread(numNodeWeights);
            for (IndexType i = 0; i < numNodeWeights; i++) {
                const auto pair = std::minmax_element(influence[i].begin(), influence[i].end());
                influenceSpread[i] = *pair.second / *pair.first;
            }

            std::vector<ValueType> weightSpread(numNodeWeights);
            for (IndexType i = 0; i < numNodeWeights; i++) {
                const auto pair = std::minmax_element(blockWeights[i].begin(), blockWeights[i].end());
                weightSpread[i] = *pair.second / *pair.first;
            }

            std::chrono::duration<ValueType,std::ratio<1>> balanceTime = std::chrono::high_resolution_clock::now() - balanceStart;
            totalBalanceTime += balanceTime.count();

            auto oldprecision = std::cout.precision(3);
            if (comm->getRank() == 0) {
                std::cout << "Iter " << iter << ", loop: " << 100*ValueType(takenLoops) / currentLocalN << "%, average comparisons: "
                          << averageComps << ", balanced blocks: " << 100*ValueType(balancedBlocks) / numNewBlocks << "%, influence spread: ";
                for (IndexType i = 0; i < numNodeWeights; i++) {
                    std::cout << influenceSpread[i] << " ";
                }
                std::cout << ", weight spread : ";
                for (IndexType i = 0; i < numNodeWeights; i++) {
                    std::cout << weightSpread[i] << " ";
                }
                std::cout << ", imbalance : ";
                for (IndexType i = 0; i < numNodeWeights; i++) {
                    std::cout << imbalance[i] << " ";
                }
                std::cout << ", time elapsed: " << totalBalanceTime << std::endl;
            }
            std::cout.precision(oldprecision);
        }

    } while ((!allWeightsBalanced) && iter < settings.balanceIterations);

    if (settings.verbose) {
        ValueType percentageSkipped = ValueType(skippedLoops*100) / (iter*localN);
        ValueType maxSkipped = comm->max(percentageSkipped);
        ValueType minSkipped = comm->min(percentageSkipped);
        ValueType avgSkipped = comm->sum(percentageSkipped) / comm->getSize();
        if (comm->getRank() == 0) {
            std::cout << "Skipped inner loops in %: " << "min: " << minSkipped << ", avg: " << avgSkipped << " " << ", max: " << maxSkipped << std::endl;
        }
    }

    // for kmeans profiling
    metrics.numBalanceIter.push_back(iter);

    return assignment;
}// assignBlocks

/**
 */
// WARNING: we do not use k as repartition assumes k=comm->getSize() and neither blockSizes and we assume
// that every block has the same size

template<typename IndexType, typename ValueType>
DenseVector<IndexType> KMeans<IndexType,ValueType>::computeRepartition(
    const std::vector<DenseVector<ValueType>>& coordinates,
    const std::vector<DenseVector<ValueType>>& nodeWeights,
    const Settings settings,
    Metrics<ValueType>& metrics) {

    const IndexType localN = coordinates[0].getLocalValues().size();
    const scai::dmemo::CommunicatorPtr comm = coordinates[0].getDistributionPtr()->getCommunicatorPtr();
    const IndexType p = comm->getSize();
    SCAI_ASSERT_EQ_ERROR(p, settings.numBlocks, "Deriving the previous partition from the distribution cannot work for p != k");
    const IndexType numNodeWeights = nodeWeights.size();

    // calculate the global weight sum to set the block sizes
    // TODO: the local weight sums are already calculated in findLocalCenters, maybe extract info from there
    std::vector<std::vector<ValueType>> blockSizes(numNodeWeights);

    for (IndexType i = 0; i < numNodeWeights; i++)
    {
        scai::hmemo::ReadAccess<ValueType> rWeights(nodeWeights[i].getLocalValues());
        SCAI_ASSERT_EQ_ERROR(rWeights.size(), localN, "Mismatch of nodeWeights and coordinates size. Check distributions.");
        //const ValueType localWeightSum = std::accumulate(rWeights.begin(), rWeights.end(), 0.0);
        const ValueType localWeightSum = scai::utilskernel::HArrayUtils::sum( nodeWeights[i].getLocalValues() );
        const ValueType globalWeightSum = comm->sum(localWeightSum);
        blockSizes[i] = std::vector<ValueType>(settings.numBlocks, globalWeightSum/settings.numBlocks);
    }

    std::chrono::time_point<std::chrono::high_resolution_clock> startCents = std::chrono::high_resolution_clock::now();
    //
    // TODO: change to findCenters
    //
    std::vector<std::vector<ValueType> > initialCenters = findLocalCenters(coordinates, nodeWeights[0]);
    std::chrono::duration<ValueType,std::ratio<1>> centTime = std::chrono::high_resolution_clock::now() - startCents;
    ValueType time = centTime.count();
    std::cout<< comm->getRank()<< ": time " << time << std::endl;

    //in this case, the previous partition is the rank of every PE
    DenseVector<IndexType> previous( coordinates[0].getDistributionPtr(), 0);
    
    {   //set previous locally
        const IndexType rank = comm->getRank();
        scai::hmemo::WriteAccess<IndexType> wLocalPrev( previous.getLocalValues() );
        for(int i=0; i<wLocalPrev.size(); i++){
            wLocalPrev[i] = rank;
        }

    }

    //give initial centers as an array; this is required because of the
    // hierarchical version
    return computePartition(coordinates, nodeWeights, blockSizes, previous, {initialCenters}, settings, metrics);
}



template<typename IndexType, typename ValueType>
DenseVector<IndexType> KMeans<IndexType,ValueType>::computeRepartition(
    const std::vector<DenseVector<ValueType>>& coordinates,
    const std::vector<DenseVector<ValueType>>& nodeWeights,
    const std::vector<std::vector<ValueType>>& blockSizes,
    const DenseVector<IndexType>& previous,
    const Settings settings) {

    const IndexType localN = previous.getLocalValues().size();
    scai::dmemo::CommunicatorPtr comm = coordinates[0].getDistributionPtr()->getCommunicatorPtr();
    std::vector<std::vector<ValueType> > initialCenters;

    if (settings.numBlocks == comm->getSize()
            && comm->all(scai::utilskernel::HArrayUtils::max(previous.getLocalValues()) == comm->getRank())
            && comm->all(scai::utilskernel::HArrayUtils::min(previous.getLocalValues()) == comm->getRank())) {
        // partition is equal to distribution
        // TODO:: change with findCenters
        initialCenters = findLocalCenters(coordinates, nodeWeights[0]);
    } else {
        std::vector<IndexType> indices(localN);
        std::iota(indices.begin(), indices.end(), 0);
        initialCenters = findCenters(coordinates, previous, settings.numBlocks, indices.begin(), indices.end(), nodeWeights[0]);
    }

    std::vector<point<ValueType>> transpCenters = vectorTranspose(initialCenters);
    SCAI_ASSERT_EQ_ERROR(transpCenters[0].size(), settings.dimensions, "Wrong centers dimension?");

    // just one group with all the centers; needed in the hierarchical version
    std::vector<std::vector<point<ValueType>>> groupOfCenters = { transpCenters };

    SCAI_ASSERT_EQ_ERROR(groupOfCenters[0][0].size(), settings.dimensions, "Wrong centers dimension?");

    Settings tmpSettings = settings;
    tmpSettings.repartition = true;

    Metrics<ValueType> metrics(settings);

    return computePartition(coordinates, nodeWeights, blockSizes, previous, groupOfCenters, tmpSettings, metrics);
}

// WARNING: if settings.repartition=true then partition has a different meaning: is the partition to be rebalanced,
// not the partition from the previous hierarchy level.
// TODO?: add another DenseVector in order not to confuse the two?

// core implementation
template<typename IndexType, typename ValueType>
DenseVector<IndexType> KMeans<IndexType,ValueType>::computePartition(
    const std::vector<DenseVector<ValueType>> &coordinates, \
    const std::vector<DenseVector<ValueType>> &nodeWeights, \
    const std::vector<std::vector<ValueType>> &targetBlockWeights, \
    const DenseVector<IndexType> &partition, // if repartition, this is the partition to be rebalanced
    std::vector<std::vector<point<ValueType>>> centers, \
    const Settings settings, \
    Metrics<ValueType>& metrics) {

    SCAI_REGION("KMeans.computePartition");
    std::chrono::time_point<std::chrono::high_resolution_clock> KMeansStart = std::chrono::high_resolution_clock::now();

    // if repartition, by convention, numOldBlocks=1=center.size()
    // the number of blocks from the previous hierarchy level
    const IndexType numOldBlocks = centers.size();
    if (settings.debugMode and not settings.repartition) {
        const IndexType maxPart = partition.max(); // global operation
        SCAI_ASSERT_EQ_ERROR(numOldBlocks-1, maxPart, "The provided partition must have equal number of blocks as the length of the vector with the new number of blocks per part");
    }

    const IndexType numNodeWeights = nodeWeights.size();
    assert(targetBlockWeights.size() == numNodeWeights);

    std::vector<bool> heterogeneousBlockSizes(numNodeWeights, false);
    for (IndexType i = 0; i < targetBlockWeights.size(); i++) {
        auto minMax = std::minmax_element(targetBlockWeights[i].begin(), targetBlockWeights[i].end());
        if (*minMax.first != *minMax.second) {
            heterogeneousBlockSizes[i] = true;
            if (settings.erodeInfluence) {
                throw std::logic_error("ErodeInfluence setting is not supported for heterogeneous blocks");
            }
        }
    }

    // the number of new blocks per old block and the total number of new blocks
    std::vector<IndexType> blockSizesPrefixSum(numOldBlocks+1, 0);
    // in a sense, this is the new k = settings.numBlocks
    IndexType totalNumNewBlocks = 0;

    // if repartition, blockSizesPrefixSum=[ 0, totalNumNewBlocks ]
    for (int b=0; b<numOldBlocks; b++) {
        blockSizesPrefixSum[b+1] += blockSizesPrefixSum[b]+centers[b].size();
        totalNumNewBlocks += centers[b].size();
    }

    // if repartition, centers1DVector=centers[0] (remember, centers.size()=1)
    // Basically, one vector with all the centers and the blockSizesPrefixSum is "useless"

    // convert to a 1D vector
    std::vector<point<ValueType>> centers1DVector;
    for (int b=0; b<numOldBlocks; b++) {
        const unsigned int k = blockSizesPrefixSum[b+1]-blockSizesPrefixSum[b];
        assert(k==centers[b].size()); // not really needed, TODO: remove?
        for (IndexType i=0; i<k; i++) {
            centers1DVector.push_back(centers[b][i]);
        }
    }
    SCAI_ASSERT_EQ_ERROR(centers1DVector.size(), totalNumNewBlocks, "Vector size mismatch");

    const IndexType dim = coordinates.size();
    assert(dim > 0);
    const IndexType localN = coordinates[0].getLocalValues().size();
    const IndexType globalN = coordinates[0].size();
    for (IndexType i = 0; i < numNodeWeights; i++) {
        SCAI_ASSERT_EQ_ERROR(nodeWeights[i].getLocalValues().size(), localN, "Mismatch between node weights and coordinate size.");
    }
    SCAI_ASSERT_EQ_ERROR(centers[0][0].size(), dim, "Center dimensions mismatch");
    SCAI_ASSERT_EQ_ERROR(centers1DVector[0].size(), dim, "Center dimensions mismatch");

    scai::dmemo::CommunicatorPtr comm = coordinates[0].getDistributionPtr()->getCommunicatorPtr();

    const IndexType p = comm->getSize();

    //
    // copy/convert node weights
    //

    std::vector<ValueType> nodeWeightSum(nodeWeights.size());
    std::vector<std::vector<ValueType>> convertedNodeWeights(nodeWeights.size());

    for (IndexType i=0; i<numNodeWeights; i++) {
        nodeWeightSum[i] = nodeWeights[i].sum();

        scai::hmemo::ReadAccess<ValueType> rWeights(nodeWeights[i].getLocalValues());
        convertedNodeWeights[i] = std::vector<ValueType>(rWeights.get(), rWeights.get()+localN);

        const ValueType blockWeightSum = std::accumulate(targetBlockWeights[i].begin(), targetBlockWeights[i].end(), 0.0);
        if (nodeWeightSum[i] > blockWeightSum*(1+settings.epsilon)) {
            for (ValueType blockSize : targetBlockWeights[i]) {
                PRINT0(std::to_string(blockSize) + " ");
            }
            throw std::invalid_argument("The total weight of the wanted blocks is " + std::to_string(blockWeightSum) + " which is smaller than the total vertex weight which is " + std::to_string(nodeWeightSum[i]) + "; i.e., the given input does not fit into the given block weights. Maybe you should try calling CommTree::adaptWeights().");
        }
    }

    // normalize node weights for adaptive influence calculation
    std::vector<std::vector<ValueType>> normalizedNodeWeights(numNodeWeights, std::vector<ValueType>(localN, 1));

    if (numNodeWeights > 1) {
        for (IndexType i = 0; i < localN; i++) {
            ValueType weightSum = 0;
            for (IndexType j = 0; j < numNodeWeights; j++) {
                weightSum += convertedNodeWeights[j][i];
            }
            for (IndexType j = 0; j < numNodeWeights; j++) {
                normalizedNodeWeights[j][i] = convertedNodeWeights[j][i] / weightSum;
            }
        }
    }

    //
    // copy coordinates
    //

    // min and max for local part of the coordinates
    std::vector<ValueType> minCoords(dim);
    std::vector<ValueType> maxCoords(dim);

    std::vector<std::vector<ValueType> > convertedCoords(dim);
    {
        for (IndexType d = 0; d < dim; d++) {
            scai::hmemo::ReadAccess<ValueType> rAccess(coordinates[d].getLocalValues());
            convertedCoords[d] = std::vector<ValueType>(rAccess.get(), rAccess.get()+localN);

            minCoords[d] = *std::min_element(convertedCoords[d].begin(), convertedCoords[d].end());
            maxCoords[d] = *std::max_element(convertedCoords[d].begin(), convertedCoords[d].end());

            assert(convertedCoords[d].size() == localN);
        }
    }

    std::vector<ValueType> globalMinCoords(dim);
    std::vector<ValueType> globalMaxCoords(dim);
    comm->minImpl(globalMinCoords.data(), minCoords.data(), dim, scai::common::TypeTraits<ValueType>::stype);
    comm->maxImpl(globalMaxCoords.data(), maxCoords.data(), dim, scai::common::TypeTraits<ValueType>::stype);

    ValueType diagonalLength = 0;
    ValueType volume = 1;
    ValueType localVolume = 1;
    for (IndexType d = 0; d < dim; d++) {
        const ValueType diff = globalMaxCoords[d] - globalMinCoords[d];
        const ValueType localDiff = maxCoords[d] - minCoords[d]; 
        diagonalLength += diff*diff;
        volume *= diff;
        localVolume *= localDiff;
    }

    // the bounding box is per PE. no need to change for the hierarchical version
    QuadNodeCartesianEuclid<ValueType> boundingBox(minCoords, maxCoords);
    if (settings.verbose) {
        std::cout << "(PE id, localN) = (" << comm->getRank() << ", "<< localN << ")" << std::endl;
        comm->synchronize();
        std::cout << "(PE id, localVolume/(volume/p) = (" << comm->getRank() << ", "<< localVolume / (volume / p) << ")" << std::endl;
    }

    diagonalLength = std::sqrt(diagonalLength);
    const ValueType expectedBlockDiameter = pow(volume /totalNumNewBlocks, 1.0/dim);

    std::vector<ValueType> upperBoundOwnCenter(localN, std::numeric_limits<ValueType>::max());
    std::vector<ValueType> lowerBoundNextCenter(localN, 0);

    //
    // prepare sampling
    //

    std::vector<IndexType> localIndices(localN);
    std::iota(localIndices.begin(), localIndices.end(), 0);

    // hierar: in the heterogenous and hierarchical case, minSamplingNodes
    // makes more sense to be a percentage of the nodes, not a number. Or not?
    // hierar: number of sampling nodes is calculated per PE, right? not per block.

    const ValueType avgBlocksPerPE = ValueType(totalNumNewBlocks)/p;
    // We can calculate precisely the number of local old blocks using "partition"
    // but we will have to go over all local points
    IndexType minNodes = settings.minSamplingNodes*avgBlocksPerPE;
    if (settings.minSamplingNodes==-1) {
        minNodes = localN;
    }

    assert(minNodes > 0);
    IndexType samplingRounds = 0;	// number of rounds needed to see all points
    std::vector<IndexType> samples;

    const bool randomInitialization = comm->all(localN > minNodes);

    // perform sampling
    {
        if (randomInitialization) {
            ITI::GraphUtils<IndexType, ValueType>::FisherYatesShuffle(localIndices.begin(), localIndices.end(), localN);
            // TODO: the cantor shuffle is more stable; random shuffling can yield better
            // results occasionally but has higher fluctuation/variance
            // localIndices = GraphUtils<IndexType,ValueType>::indexReorderCantor(localN);

            SCAI_ASSERT_EQ_ERROR(*std::max_element(localIndices.begin(), localIndices.end()), localN -1, "Error in index reordering");
            SCAI_ASSERT_EQ_ERROR(*std::min_element(localIndices.begin(), localIndices.end()), 0, "Error in index reordering");

            samplingRounds = std::ceil(std::log2(globalN / ValueType(settings.minSamplingNodes*totalNumNewBlocks)))+1;

            samples.resize(samplingRounds);
            samples[0] = std::min(minNodes, localN);
        }

        if (settings.verbose) {
            PRINT0(*comm << ": localN= "<< localN << ", minNodes= " << minNodes << ", samplingRounds= " << samplingRounds << ", lastIndex: " << *localIndices.end());
        }
        if (samplingRounds > 0 && settings.verbose) {
            if (comm->getRank() == 0) std::cout << "Starting with " << samplingRounds << " sampling rounds." << std::endl;
        }
        // double the number of samples per round
        for (IndexType i = 1; i < samplingRounds; i++) {
            samples[i] = std::min(IndexType(samples[i-1]*2), localN);
        }
        if (samplingRounds > 0) {
            samples[samplingRounds-1] = localN;
        }
    }

    IndexType iter = 0;
    ValueType delta = 0;
    bool balanced = false;
    const ValueType threshold = 0.002*diagonalLength;// TODO: take global point density into account
    const IndexType maxIterations = settings.maxKMeansIterations;
    const typename std::vector<IndexType>::iterator firstIndex = localIndices.begin();
    typename std::vector<IndexType>::iterator lastIndex = localIndices.end();
    std::vector<ValueType> imbalances(numNodeWeights, 1);

    std::vector<std::vector<ValueType>> influence(numNodeWeights, std::vector<ValueType>(totalNumNewBlocks, 1));

    // result[i]=b, means that point i belongs to cluster/block b
    DenseVector<IndexType> result(coordinates[0].getDistributionPtr(), 0);

    // TODO, recheck:
    // if repartition, should it be result = previous???
    if (settings.repartition) {
        assert(partition.getDistributionPtr()->isEqual(*coordinates[0].getDistributionPtr()));
        result = partition;
    }

    do {
        std::chrono::time_point<std::chrono::high_resolution_clock> iterStart = std::chrono::high_resolution_clock::now();
        if (iter < samplingRounds) {
            SCAI_ASSERT_LE_ERROR(samples[iter], localN, "invalid number of samples");
            lastIndex = localIndices.begin() + samples[iter];
            std::sort(localIndices.begin(), lastIndex);// sorting not really necessary, but increases locality
            [[maybe_unused]] ValueType ratio = ValueType(comm->sum(samples[iter])) / globalN;
            assert(ratio <= 1);

        } else {
            SCAI_ASSERT_EQ_ERROR(lastIndex - firstIndex, localN, "invalid iterators");
            assert(lastIndex == localIndices.end());
        }

        std::vector<std::vector<ValueType>> adjustedBlockSizes(numNodeWeights);

        for (IndexType i = 0; i < numNodeWeights; i++) {

            ValueType localSampleWeightSum = 0;
            {
                scai::hmemo::ReadAccess<ValueType> rWeights(nodeWeights[i].getLocalValues());
                for (auto it = firstIndex; it != lastIndex; it++) {
                    localSampleWeightSum += rWeights[*it];
                }
            }

            const ValueType totalSampledWeightSum = comm->sum(localSampleWeightSum);
            const ValueType ratio = totalSampledWeightSum / nodeWeightSum[i];
            adjustedBlockSizes[i].resize(targetBlockWeights[i].size());

            //TODO: merge to one assertion; or not...
            if( std::is_same<ValueType,float>::value ){
                //SCAI_ASSERT_LE_ERROR(totalSampledWeightSum, nodeWeightSum[i]*(1+1e-4), "Error in sampled weight sum.");
            }else{  //double
                SCAI_ASSERT_LE_ERROR(totalSampledWeightSum, nodeWeightSum[i]*(1+1e-8), "Error in sampled weight sum.");
            }


            for (IndexType j = 0; j < targetBlockWeights[i].size(); j++) {
                adjustedBlockSizes[i][j] = ValueType(targetBlockWeights[i][j]) * ratio;
                if (settings.verbose && iter < samplingRounds) {
                    if (j == 0 || heterogeneousBlockSizes[i]) {
                        PRINT0("Adjusted " + std::to_string(targetBlockWeights[i][j]) + " down to " + std::to_string(adjustedBlockSizes[i][j]));
                    }
                }
            }
        }

        std::vector<ValueType> timePerPE(comm->getSize(), 0.0);

        result = assignBlocks(convertedCoords, centers1DVector, blockSizesPrefixSum, firstIndex, lastIndex, convertedNodeWeights, normalizedNodeWeights, result, partition, adjustedBlockSizes, boundingBox, upperBoundOwnCenter, lowerBoundNextCenter, influence, imbalances, settings, metrics);

        scai::hmemo::ReadAccess<IndexType> rResult(result.getLocalValues());

        // TODO: too much info? remove?
        if (settings.verbose and settings.debugMode) {
            comm->sumImpl(timePerPE.data(), timePerPE.data(), comm->getSize(), scai::common::TypeTraits<ValueType>::stype);
            if (comm->getRank()==0) {
                vector<IndexType> indices(timePerPE.size());
                std::iota(indices.begin(), indices.end(), 0);
                std::sort(indices.begin(), indices.end(),
                [&timePerPE](int i, int j) {
                    return timePerPE[i]<timePerPE[j];
                });

                for (int i=0; i<comm->getSize(); i++) {
                    std::cout << indices[i]<< ": time for PE: " << timePerPE[indices[i]] << std::endl;
                    std::cout << "(" << indices[i] << "," << timePerPE[indices[i]] << ")" << std::endl;
                }
            }
        }

        // TODO: adapt for multiple weights
        std::vector<std::vector<ValueType>> newCenters = findCenters(coordinates, result, totalNumNewBlocks, firstIndex, lastIndex, nodeWeights[0]);

        // newCenters have reversed order of the vectors
        // maybe turn centers to a 1D vector already in computePartition?

        std::vector<point<ValueType>> transCenters = vectorTranspose(newCenters);
        assert(transCenters.size()==totalNumNewBlocks);
        assert(transCenters[0].size()==dim);

        // keep centroids of empty blocks at their last known position
        for (IndexType j = 0; j <totalNumNewBlocks; j++) {
            // center for block j is empty
            if (std::isnan(transCenters[j][0])) {
                transCenters[j] = centers1DVector[j];
            }
        }
        std::vector<ValueType> squaredDeltas(totalNumNewBlocks,0);
        std::vector<ValueType> deltas(totalNumNewBlocks,0);
        std::vector<std::vector<ValueType>> oldInfluence = influence;
        ValueType minRatio = std::numeric_limits<ValueType>::max();

        for (IndexType j = 0; j < totalNumNewBlocks; j++) {
            for (int d = 0; d < dim; d++) {
                SCAI_ASSERT_LE_ERROR(transCenters[j][d], globalMaxCoords[d]+ 1e-6, "New center coordinate out of bounds");
                SCAI_ASSERT_GE_ERROR(transCenters[j][d], globalMinCoords[d]- 1e-6, "New center coordinate out of bounds");
                ValueType diff = (centers1DVector[j][d] - transCenters[j][d]);
                squaredDeltas[j] += diff*diff;
            }
            deltas[j] = std::sqrt(squaredDeltas[j]);
            if (settings.erodeInfluence) {
                const ValueType erosionFactor = 2/(1+exp(-std::max(deltas[j]/expectedBlockDiameter-ValueType(0.1), ValueType(0.0)))) - 1;
                for (IndexType i = 0; i < numNodeWeights; i++) {
                    influence[i][j] = exp((1-erosionFactor)*log(influence[i][j]));
                    if (oldInfluence[i][j] / influence[i][j] < minRatio) minRatio = oldInfluence[i][j] / influence[i][j];
                }
            }
        }

        centers1DVector = transCenters;

        delta = *std::max_element(deltas.begin(), deltas.end());
        assert(delta >= 0);
        const ValueType deltaSq = delta*delta;
        ValueType maxInfluence = 0;
        for (IndexType w = 0; w < numNodeWeights; w++) {
            maxInfluence = std::max(maxInfluence, *std::max_element(influence[w].begin(), influence[w].end()));
        }

        {
            SCAI_REGION("KMeans.computePartition.updateBounds");
            for (auto it = firstIndex; it != lastIndex; it++) {
                const IndexType i = *it;
                IndexType cluster = rResult[i];
                assert(cluster<totalNumNewBlocks);

                ValueType influenceEffect = 0;
                for (IndexType w = 0; w < numNodeWeights; w++) {
                    influenceEffect += influence[w][cluster]*normalizedNodeWeights[w][i];
                }

                if (settings.erodeInfluence) {
                    // WARNING: erodeInfluence not supported for hierarchical version
                    // TODO: or it is?? or should it be??

                    if (numNodeWeights > 0) throw std::logic_error("Influence erosion not yet implemented for multiple weights.");

                    // update due to erosion
                    upperBoundOwnCenter[i] *= (influence[0][cluster] / oldInfluence[0][cluster]) + 1e-6;
                    lowerBoundNextCenter[i] *= minRatio - 1e-6;
                }

                // update due to delta
                upperBoundOwnCenter[i] += (2*deltas[cluster]*std::sqrt(upperBoundOwnCenter[i]/influenceEffect) + squaredDeltas[cluster])*(influenceEffect + 1e-6);

                ValueType pureSqrt(std::sqrt(lowerBoundNextCenter[i]/maxInfluence));
                if (pureSqrt < delta) {
                    lowerBoundNextCenter[i] = 0;
                } else {
                    ValueType diff = (-2*delta*pureSqrt + deltaSq)*(maxInfluence + 1e-6);
                    assert(diff <= 0);
                    lowerBoundNextCenter[i] += diff;
                    if (!(lowerBoundNextCenter[i] > 0)) lowerBoundNextCenter[i] = 0;
                }

                assert(std::isfinite(lowerBoundNextCenter[i]));
            }
        }

        // find local weight of each block
        std::vector<std::vector<ValueType>> currentBlockWeights(numNodeWeights, std::vector<ValueType>(totalNumNewBlocks,0.0));
        for (IndexType j = 0; j < numNodeWeights; j++) {
            scai::hmemo::ReadAccess<ValueType> rWeights(nodeWeights[j].getLocalValues());
            for (auto it = firstIndex; it != lastIndex; it++) {
                const IndexType i = *it;
                IndexType cluster = rResult[i];
                currentBlockWeights[j][cluster] += rWeights[i];
            }
        }

        // print times before global reduce step
        // aux<IndexType,ValueType>::timeMeasurement(iterStart);
        std::chrono::duration<ValueType,std::ratio<1>> balanceTime = std::chrono::high_resolution_clock::now() - iterStart;
        ValueType time = balanceTime.count();

        if (settings.verbose) {
            PRINT0(*comm <<": in computePartition, iteration time: " << time);
        }

        {
            SCAI_REGION("KMeans.computePartition.currentBlockWeightSum");
            for (IndexType i = 0; i < numNodeWeights; i++) {
                comm->sumImpl(currentBlockWeights[i].data(), currentBlockWeights[i].data(), totalNumNewBlocks, scai::common::TypeTraits<ValueType>::stype);
            }
        }

        // check if all blocks are balanced
        balanced = true;
        for (IndexType i = 0; i < numNodeWeights; i++) {
            for (IndexType j=0; j<totalNumNewBlocks; j++) {
                if (currentBlockWeights[i][j] > adjustedBlockSizes[i][j]*(1+settings.epsilon)) {
                    balanced = false;
                }
            }
        }

        ValueType maxTime=0;
        if (settings.verbose) {
            balanceTime = std::chrono::high_resolution_clock::now() - iterStart;
            maxTime = comm->max(balanceTime.count());
        }

        if (comm->getRank() == 0) {
            std::cout << "i: " << iter<< ", delta: " << delta << ", imbalance=";
            for (IndexType i = 0; i < numNodeWeights; i++) {
                std::cout << " " << imbalances[i];
            }
            if (settings.verbose) {
                std::cout << ", time : "<< maxTime;
            }
            std::cout << std::endl;
        }

        metrics.kmeansProfiling.push_back(std::make_tuple(delta, maxTime, imbalances[0]));

        iter++;

    } while (iter < samplingRounds or (iter < maxIterations && (delta > threshold || !balanced)));


    std::chrono::duration<ValueType,std::ratio<1>> KMeansTime = std::chrono::high_resolution_clock::now() - KMeansStart;
    ValueType time = comm->max(KMeansTime.count());

    PRINT0("total KMeans time: " << time << " , number of iterations: " << iter);
    //special time for the core kmeans
    metrics.MM["timeKmeans"] = time;

    return result;
}// computePartition


template<typename IndexType, typename ValueType>
DenseVector<IndexType> KMeans<IndexType,ValueType>::computePartition(
    const std::vector<DenseVector<ValueType>> &coordinates,
    const Settings settings) {

    const scai::dmemo::DistributionPtr dist = coordinates[0].getDistributionPtr();
    const IndexType globalN = dist->getGlobalSize();
    const scai::lama::DenseVector<ValueType> unitNodeWeights = scai::lama::DenseVector<ValueType>(dist, 1);
    const std::vector<scai::lama::DenseVector<ValueType>> nodeWeights = {unitNodeWeights};
    std::vector<std::vector<ValueType>> blockSizes(1, std::vector<ValueType>(settings.numBlocks, std::ceil(globalN/settings.numBlocks)));
    Metrics<ValueType> metrics(settings);

    return computePartition(coordinates, nodeWeights, blockSizes, settings, metrics);
}


// wrapper 1 - called initially with no centers parameter
template<typename IndexType, typename ValueType>
DenseVector<IndexType> KMeans<IndexType,ValueType>::computePartition(
    const std::vector<DenseVector<ValueType>> &coordinates,
    const std::vector<DenseVector<ValueType>> &nodeWeights,
    const std::vector<std::vector<ValueType>> &blockSizes,
    const Settings settings,
    Metrics<ValueType>& metrics) {

    std::vector<ValueType> minCoords(settings.dimensions);
    std::vector<ValueType> maxCoords(settings.dimensions);
    std::tie(minCoords, maxCoords) = getGlobalMinMaxCoords(coordinates);

    std::vector<point<ValueType>> centers = findInitialCentersSFC(coordinates, minCoords, maxCoords, settings);
    SCAI_ASSERT_EQ_ERROR(centers.size(), settings.numBlocks, "Number of centers is not correct");
    SCAI_ASSERT_EQ_ERROR(centers[0].size(), settings.dimensions, "Dimension of centers is not correct");

    // just one group with all the centers; needed in the hierarchical version
    std::vector<std::vector<point<ValueType>>> groupOfCenters = { centers };

    // every point belongs to one block in the beginning
    scai::lama::DenseVector<IndexType> partition(coordinates[0].getDistributionPtr(), 0);

    return computePartition(coordinates, nodeWeights, blockSizes, partition, groupOfCenters, settings, metrics);
}


// ---------------------------------------
template<typename IndexType, typename ValueType>
DenseVector<IndexType> KMeans<IndexType,ValueType>::computeHierarchicalPartition(
    std::vector<DenseVector<ValueType>> &coordinates,
    std::vector<DenseVector<ValueType>> &nodeWeights,
    const CommTree<IndexType,ValueType> &commTree,
    Settings settings,
    Metrics<ValueType>& metrics) {

    typedef cNode<IndexType,ValueType> cNode;

    // check although numBlocks is not needed or used
    SCAI_ASSERT_EQ_ERROR(settings.numBlocks, commTree.getNumLeaves(), "The number of leaves and number of blocks must agree");

    // get global communicator
    scai::dmemo::CommunicatorPtr comm = coordinates[0].getDistributionPtr()->getCommunicatorPtr();

    if (settings.erodeInfluence) {
        if (comm->getRank()==0) {
            std::cout << "WARNING: erode influence is not supported for the hierarchical version. Will set it to false and continue." << std::endl;
        }
        settings.erodeInfluence = false;
    }

    const IndexType numNodeWeights = nodeWeights.size();
    const scai::dmemo::DistributionPtr dist = coordinates[0].getDistributionPtr();

    // redistribute points based on their hilbert curve index
    // warning: this functions redistributes the coordinates and the node weights.
    // TODO: is this supposed to be here? it is also in ParcoRepart::partitionGraph

    HilbertCurve<IndexType,ValueType>::redistribute(coordinates, nodeWeights, settings, metrics);

    if (settings.debugMode) {
        // added check to verify that the points are indeed distributed
        // based on the hilbert curve. Otherwise, the prefix sum needed to
        // calculate the centers, does not have the desired meaning.
        for (IndexType i=0; i<numNodeWeights; i++) {
            bool hasHilbertDist = HilbertCurve<IndexType, ValueType>::confirmHilbertDistribution(coordinates, nodeWeights[i], settings);
            SCAI_ASSERT_EQ_ERROR(hasHilbertDist, true, "Input must be distributed according to a hilbert curve distribution");
        }
    }

    std::vector<ValueType> minCoords(settings.dimensions);
    std::vector<ValueType> maxCoords(settings.dimensions);
    std::tie(minCoords, maxCoords) = getGlobalMinMaxCoords(coordinates);

    // used later for debugging and calculating imbalance
    std::vector<ValueType> totalWeightSum(numNodeWeights);
    for (IndexType i = 0; i < numNodeWeights; i++)
    {
        scai::hmemo::ReadAccess<ValueType> rW(nodeWeights[i].getLocalValues());
        ValueType localW = 0;
        for (IndexType j=0; j<rW.size(); j++) {
            localW += rW[j];
        }
        totalWeightSum[i] = comm->sum(localW);
    }

    // typedef of commNode from CommTree.h, see also KMeans.h
    cNode root = commTree.getRoot();
    if (settings.debugMode) {
        PRINT("Starting hierarchical KMeans.\nRoot node: ");
        root.print();
    }

    // every point belongs to one block in the beginning
    scai::lama::DenseVector<IndexType> partition(coordinates[0].getDistributionPtr(), 0);

    // skip root. If we start from the root, we will know the number
    // of blocks but not the memory and speed per block
    for (unsigned int h=1; h<commTree.getNumHierLevels(); h++) {

        /*
        There are already as many blocks as the number of leaves
        of the previous hierarchy level. The new number of blocks per
        old block is prevLeaf.numChildren. Example, if previous level
        had 3 leaves with 4, 6 and 10 children respectively, then
        the number of new blocks that we will partition in this step is
        4 for block 0, 6 for block 1 and 10 for block 2, in total 20.
        */

        // in how many blocks each known block (from the previous hier level) will be partitioned
        // numNewBlocksPerOldBlock[i]=k means that, current/old block i should be
        // partitioned into k new blocks

        std::vector<cNode> thisLevel = commTree.getHierLevel(h);

        PRINT0("-- Hierarchy level " << h << " with " << thisLevel.size() << " nodes");
        // TODO: probably too verbosy, remove
        if (settings.debugMode) {
            PRINT0("******* in debug mode");
            for (cNode c: thisLevel) { // print all nodes of this level
                c.print();
            }
        }

        //
        // 1- find initial centers for this hierarchy level
        //
        // Only the new level is passed and the previous level is
        // reconstructed internally

        std::vector<std::vector<point<ValueType>>> groupOfCenters = findInitialCentersSFC(coordinates, minCoords, maxCoords, partition, thisLevel, settings);

        SCAI_ASSERT_EQ_ERROR(groupOfCenters.size(), commTree.getHierLevel(h-1).size(), "Wrong number of blocks calculated");
        if (settings.debugMode) {
            PRINT0("******* in debug mode");
            IndexType sumNumCenters = 0;
            for (int g=0; g<groupOfCenters.size(); g++) {
                sumNumCenters += groupOfCenters[g].size();
            }
            SCAI_ASSERT_EQ_ERROR(sumNumCenters, thisLevel.size(), "Mismatch in number of new centers and hierarchy nodes")
        }

        // number of old, known blocks == previous level size
        IndexType numOldBlocks = groupOfCenters.size();

        // number of new blocks each old blocks must be partitioned to
        std::vector<unsigned int> numNewBlocks = commTree.getGrouping(thisLevel);
        SCAI_ASSERT_EQ_ERROR(numOldBlocks, numNewBlocks.size(), "Hierarchy level size mismatch");
        const IndexType totalNumNewBlocks = std::accumulate(numNewBlocks.begin(), numNewBlocks.end(), 0);

        if (settings.debugMode) {
            const IndexType maxPart = partition.max(); // global operation
            SCAI_ASSERT_EQ_ERROR(numOldBlocks-1, maxPart, "The provided partition must have equal number of blocks as the length of the vector with the new number of blocks per part");
        }

        //
        // 2- main k-means loop
        //

        // get the wanted block sizes for this level of the tree
        std::vector<std::vector<ValueType>> targetBlockWeights = commTree.getBalanceVectors(h);
        SCAI_ASSERT_EQ_ERROR(targetBlockWeights.size(), numNodeWeights, "Wrong number of weights");
        SCAI_ASSERT_EQ_ERROR(targetBlockWeights[0].size(), totalNumNewBlocks, "Wrong size of weights");
        // PRINT0(h << ": " << std::accumulate(targetBlockWeights[0].begin(), targetBlockWeights[0].end(), 0.0));
        // TODO: inside computePartition, settings.numBlocks is not
        // used. We infer the number of new blocks from the groupOfCenters
        // maybe, set also numBlocks for clarity??

        partition = computePartition(coordinates, nodeWeights, targetBlockWeights, partition, groupOfCenters, settings, metrics);

        // TODO: not really needed assertions
        SCAI_ASSERT_EQ_ERROR(coordinates[0].getDistributionPtr()->getLocalSize(),\
                             partition.getDistributionPtr()->getLocalSize(), "Partition distribution mismatch(?)");
        SCAI_ASSERT_EQ_ERROR(nodeWeights[0].getDistributionPtr()->getLocalSize(),\
                             partition.getDistributionPtr()->getLocalSize(), "Partition distribution mismatch(?)");

        if (settings.debugMode) {
            // this check is done before. TODO: remove?
            const IndexType maxPart = partition.max(); // global operation
            SCAI_ASSERT_EQ_ERROR(totalNumNewBlocks-1, maxPart, "The provided old assignment must have equal number of blocks as the length of the vector with the new number of blocks per part");
            if (settings.storeInfo) {
                // FileIO<IndexType,ValueType>::writePartitionParallel(partition, "./partResults/partHKM"+std::to_string(settings.numBlocks)+"_h"+std::to_string(h)+".out");
                FileIO<IndexType,ValueType>::writeDenseVectorCentral(partition, "./partResults/partHKM"+std::to_string(settings.numBlocks)+"_h"+std::to_string(h)+".out");
            }
        }

        // TODO?: remove?
        std::vector<ValueType> imbalances(numNodeWeights);
        for (IndexType i = 0; i < numNodeWeights; i++) {
            imbalances[i] = ITI::GraphUtils<IndexType, ValueType>::computeImbalance(partition, totalNumNewBlocks, nodeWeights[i], targetBlockWeights[i]);
        }

        PRINT0("\nFinished hierarchy level " << h <<", partitioned into " << totalNumNewBlocks << " blocks and imbalance is:");
        if (comm->getRank()==0) {
            for (IndexType i = 0; i < imbalances.size(); i++) {
                std::cout<< " " << imbalances[i] <<std::endl;
            }
        }

    } // for (h=1; h<commTree.hierarchyLevels; h++){

    return partition;
}// computeHierarchicalPartition

// --------------------------------------------------------------------------
template<typename IndexType, typename ValueType>
DenseVector<IndexType> KMeans<IndexType,ValueType>::computeHierPlusRepart(
    std::vector<DenseVector<ValueType>> &coordinates,
    std::vector<DenseVector<ValueType>> &nodeWeights,
    const CommTree<IndexType,ValueType> &commTree,
    Settings settings,
    Metrics<ValueType>& metrics) {

    // get a hierarchical partition
    DenseVector<IndexType> result = computeHierarchicalPartition(coordinates, nodeWeights, commTree, settings, metrics);

    std::vector<std::vector<ValueType>> blockSizes = commTree.getBalanceVectors(-1);

    const scai::dmemo::CommunicatorPtr comm = coordinates[0].getDistributionPtr()->getCommunicatorPtr();
    PRINT0("Finished hierarchical partition");

    // refine using a repartition step

    std::chrono::time_point<std::chrono::high_resolution_clock> repartStart = std::chrono::high_resolution_clock::now();
    DenseVector<IndexType> result2 = computeRepartition(coordinates, nodeWeights, blockSizes, result, settings);
    std::chrono::duration<ValueType,std::ratio<1>> repartTime = std::chrono::high_resolution_clock::now() - repartStart;
    metrics.MM["timeKmeans"] += repartTime.count();

    return result2;
}// computeHierPlusRepart

/* Get local minimum and maximum coordinates
 * TODO: This isn't used any more! Remove?
 */
template<typename IndexType, typename ValueType>
std::pair<std::vector<ValueType>,std::vector<ValueType>> KMeans<IndexType,ValueType>::getGlobalMinMaxCoords(const std::vector<DenseVector<ValueType>> &coordinates) {
    const int dim = coordinates.size();
    std::vector<ValueType> minCoords(dim);
    std::vector<ValueType> maxCoords(dim);
    for (int d = 0; d < dim; d++) {
        minCoords[d] = coordinates[d].min();
        maxCoords[d] = coordinates[d].max();
        SCAI_ASSERT_NE_ERROR(minCoords[d], maxCoords[d], "min=max for dimension "<< d << ", this will cause problems to the hilbert index. local= " << coordinates[0].getLocalValues().size());
    }
    return {minCoords, maxCoords};
}

//
// instantiations
//


template class KMeans<IndexType, double>;
template class KMeans<IndexType, float>;

} /* namespace ITI */
