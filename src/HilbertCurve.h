/*
 * HilbertCurve.h
 *
 *  Created on: 15.11.2016
 *      Author: Charilaos Tzovas
 */

#pragma once

#include <assert.h>
#include <cmath>
#include <climits>
#include <queue>
#include <algorithm>

#include <scai/lama.hpp>
#include <scai/lama/matrix/all.hpp>
#include <scai/lama/Vector.hpp>

#include <scai/dmemo/Distribution.hpp>
#include <scai/dmemo/HaloExchangePlan.hpp>
#include <scai/dmemo/Distribution.hpp>
#include <scai/dmemo/BlockDistribution.hpp>
#include <scai/dmemo/GenBlockDistribution.hpp>

#include <scai/sparsekernel/openmp/OpenMPCSRUtils.hpp>
#include <scai/tracing.hpp>

#include <JanusSort.hpp>

#include "Settings.h"
#include "Metrics.h"


namespace ITI {

using scai::lama::DenseVector;


/** @cond INTERNAL
*/
template <typename ValueType>
struct sort_pair {
    ValueType value;
    int32_t index;
    bool operator<(const sort_pair<ValueType>& rhs ) const {
        return value < rhs.value || (value == rhs.value && index < rhs.index);
    }
    bool operator>(const sort_pair<ValueType>& rhs ) const {
        return value > rhs.value || (value == rhs.value && index > rhs.index);
    }
    bool operator<=(const sort_pair<ValueType>& rhs ) const {
        return !operator>(rhs);
    }
    bool operator>=(const sort_pair<ValueType>& rhs ) const {
        return !operator<(rhs);
    }
};


/** @brief Class providing functionality to calculate the hilbert index (and the inverse)
of 2 or 3 dimensional points.

 The hilbert index is the index of a point in the
<a href="https://en.wikipedia.org/wiki/Hilbert_curve"> hilbert curve</a>.
*/

template <typename IndexType, typename ValueType>
class HilbertCurve {

public:

    /**
     * @brief Partition a point set using the Hilbert curve, only implemented for equal number of blocks and processes.
     *
     * @param coordinates Coordinates of the input points
     * @param settings Settings struct
     *
     * @return partition DenseVector, redistributed according to the partition
     */
    static scai::lama::DenseVector<IndexType> computePartition(const std::vector<DenseVector<ValueType>> &coordinates, Settings settings);

    /** \overload
    @param[in] nodeWeights Weights for the points
    */
    /*
    * Get an initial partition using the Hilbert curve.
    * TODO: This currently does nothing and isn't used. Remove?
    */
    static scai::lama::DenseVector<IndexType> computePartition(const std::vector<DenseVector<ValueType>> &coordinates, const DenseVector<ValueType> &nodeWeights, Settings settings);


    /** @brief Accepts a 2D/3D point and calculates its hilbert index.
    *
    * @param[in] point Node positions. In d dimensions, coordinates of node v are at v*d ... v*d+(d-1).
    * @param[in] dimensions Number of dimensions of coordinates.
    * @param[in] recursionDepth The number of refinement levels the hilbert curve should have
    * @param[in] minCoords A vector containing the minimal value for each dimension
    * @param[in] maxCoords A vector containing the maximal value for each dimension
    *
    * @return A value in the unit interval [0,1]
    */
    static double getHilbertIndex(ValueType const *point, const IndexType dimensions, const IndexType recursionDepth, const std::vector<ValueType> &minCoords, const std::vector<ValueType> &maxCoords);

    /** @brief Gets a vector of 2D/3D coordinates and returns a vector with the  hilbert indices for all coordinates.
     *
     * @param[in] coordinates The coordinates of all the points
     * @param[in] recursionDepth The number of refinement levels the hilbert curve should have
     * @param[in] dimensions Number of dimensions of coordinates.
     *
     * @return A vector with the hilbert indices for every local point. return.size()=coordinates[0].size()
     */
    static std::vector<double> getHilbertIndexVector (const std::vector<DenseVector<ValueType>> &coordinates, IndexType recursionDepth, const IndexType dimensions);

    //
    //reverse: from hilbert index to 2D/3D point
    //

    /**	@brief Given a number between 0 and 1, it returns a 2D/3D point according to the hilbert curve.
     *
     * @param[in] index The input index
     * @param[in] recursionDepth The number of refinement levels of the hilbert curve
     * @param[in] dimensions Number of dimensions of coordinates.
     *
     * @return A vector with the points coordinates. return.size()=dimensions
     */
    static std::vector<ValueType> HilbertIndex2Point(const ValueType index, const IndexType recursionDepth, const IndexType dimensions);

    /**	@brief Given a vector of numbers between 0 and 1, it returns a vector of 2D/3D points according to the hilbert curve.
     *
     * @param[in] indices The vector of indices
     * @param[in] recursionDepth The number of refinement levels of the hilbert curve
     * @param[in] dimensions Number of dimensions of coordinates.
     *
     * @return A vector of 2D/3D point coordinates. return.size()=indices.size() and return[i].size()=dimensions
     */
    static std::vector<std::vector<ValueType>> HilbertIndex2PointVec(const std::vector<ValueType> indices, const IndexType recursionDepth, const IndexType dimensions);


    /** Get the hilbert indices sorted. Every PE will own its part of the hilbert indices.
     * Warning: Internaly, the sorting algorithm redistributes the returned vector so the local size of coordinates and the returned vector maybe do not agree.
     * return[i] is a sort_pair with:
     *	return[i].index = global id/index in the distribution of a point p
     * 	return[i].value = the hilbert index of point p
     *
     * Example: before sorting, take point p=(x,y) with its global id/index k, thus x=coordinates[0][k], y=coordinates[1][k]. And return[k].index = k, return[k].value = hilbertIndex(p)
     *
     * After sorting (this is the returned vector), the pair of point p ended up is some position i.
     * So i and k=return[i].index are unrelated
     *
     * @param[in] coordinates The coordinates of all the points
     * @return A sorted vector based on the hilbert index of each point.
     */
    static std::vector<sort_pair<ValueType>> getSortedHilbertIndices( const std::vector<DenseVector<ValueType>> &coordinates, Settings settings);

    /** Redistribute coordinates and weights according to an implicit hilberPartition.
     * Equivalent to (but faster):
     *  partition = hilbertPartition(coordinates, settings)
     *  for (IndexType d = 0; d < settings.dimensions; d++) {
     *      coordinates.redistribute(partition.getDistributionPtr());
     *  }
     *  nodeWeights.redistribute(partition.getDistributionPtr());
     *
     *  @param[in,out] coordinates Coordinates of input points, will be redistributed
     *  @param[in,out] nodeWeights NodeWeights of input points, will be redistributed
     *  @param[in] settings Settings struct, effectively only needed for the hilbert curve resolution
     *  @param[out] metrics
     */
    static void redistribute(std::vector<DenseVector<ValueType> >& coordinates, std::vector<DenseVector<ValueType>>& nodeWeights, Settings settings, Metrics<ValueType>& metrics);

    /** @brief Checks if all the input data are distributed to PEs according to the hilbert index curve of the coordinates

     *  @param[in,out] coordinates Coordinates of input points, will be redistributed
     *  @param[in,out] nodeWeights NodeWeights of input points, will be redistributed
     *  @param[in] settings Settings struct, effectively only needed for the hilbert curve resolution

     @return true if the coordinates are distributed among PEs based on their hilbert index, false otherwise.
    */
    static bool confirmHilbertDistribution(
        //const scai::lama::CSRSparseMatrix<ValueType> &graph,
        const std::vector<DenseVector<ValueType>> &coordinates,
        const DenseVector<ValueType> &nodeWeights,
        Settings settings);


private:
    /** @brief Accepts a 2D point and returns is hilbert index.
     */
    static double getHilbertIndex2D(ValueType const * point, IndexType dimensions, IndexType recursionDepth, const std::vector<ValueType> &minCoords, const std::vector<ValueType> &maxCoords);

    /** @brief Gets a vector of coordinates in 2D as input and returns a vector with the hilbert indices for all coordinates.
     */
    static std::vector<double> getHilbertIndex2DVector (const std::vector<DenseVector<ValueType>> &coordinates, IndexType recursionDepth);
    /**
    *@brief Accepts a point in 3 dimensions and calculates where along the hilbert curve it lies.
    *
    * @param[in] coordinates Node positions. In d dimensions, coordinates of node v are at v*d ... v*d+(d-1).
    * @param[in] dimensions Number of dimensions of coordinates.
    * @param[in] index The index of the points whose hilbert index is desired
    * @param[in] recursionDepth The number of refinement levels the hilbert curve should have
    * @param[in] minCoords A vector containing the minimal value for each dimension
    * @param[in] maxCoords A vector containing the maximal value for each dimension
    *
    * @return A value in the unit interval [0,1]
    */
    static double getHilbertIndex3D(ValueType const * point, IndexType dimensions, IndexType recursionDepth, const std::vector<ValueType> &minCoords, const std::vector<ValueType> &maxCoords);

    /* Gets a vector of coordinates (either 2D or 3D) as input and returns a vector with the
     * hilbert indices for all coordinates.
     */
    static std::vector<double> getHilbertIndex3DVector (const std::vector<DenseVector<ValueType>> &coordinates, IndexType recursionDepth);

    //
    //reverse: from hilbert index to 2D/3D point
    //

    /**
    * Given an index between 0 and 1 returns a point in 2 dimensions along the hilbert curve based on
    * the recursion depth. Mostly for test reasons.
    * @param[in] index The index in the hilbert curve, a number in [0,1].
    * @param[in] recursionDepth The number of refinement levels the hilbert curve should have
    *
    * @return A point in the unit square [0,1]^2.
    */
    static std::vector<ValueType> Hilbert2DIndex2Point(const ValueType index, const IndexType recursionDepth );

    /**
    * Given a vector of indices between 0 and 1 returns a vector of points in 2 dimensions along the hilbert curve based on
    * the recursion depth. Mostly for test reasons.
    * @param[in] index The index in the hilbert curve, a number in [0,1].
    * @param[in] recursionDepth The number of refinement levels the hilbert curve should have
    *
    * @return A point in the unit square [0,1]^2.
    */
    static std::vector<std::vector<ValueType>> Hilbert2DIndex2PointVec(const std::vector<ValueType> indices, IndexType recursionDepth);


    /**
    * Given an index between 0 and 1 returns a point in 3 dimensions along the hilbert curve based on
    * the recursion depth. Mostly for test reasons.
    * @param[in] index The index in the hilbert curve, a number in [0,1].
    * @param[in] recursionDepth The number of refinement levels the hilbert curve should have
    *
    * @return A point in the unit cube [0,1]^3
    */
    static std::vector<ValueType> Hilbert3DIndex2Point(const ValueType index, const IndexType recursionDepth);

    /** Similar, but for 3D, as Hilbert2DIndex2PointVec()
    */
    static std::vector<std::vector<ValueType>> Hilbert3DIndex2PointVec(const std::vector<ValueType> indices, IndexType recursionDepth);

};


template<typename T>
MPI_Datatype getMPIType();

template<typename T1, typename T2>
MPI_Datatype getMPITypePair();


}//namespace ITI
