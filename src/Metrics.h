#pragma once

#include <numeric>
#include <math.h>
#include <scai/lama.hpp>
#include <scai/dmemo/RedistributePlan.hpp>
#include <chrono>
#include <algorithm>

#include "GraphUtils.h"

namespace ITI {

/** @brief A struct to hold and calculate several metrics for the partition.
*/
struct Metrics {

    // timing results
    //

    //vector specific for kmeans
    // tuple has 3 values: (delta, maxTime, imbalance)
    std::vector<std::tuple<ValueType, ValueType, ValueType>> kmeansProfiling; // specific for k-means profiling
    std::vector<IndexType> numBalanceIter;

    std::vector< std::vector<std::pair<ValueType,ValueType>> > localRefDetails; // specific for local refinement profiilng

    // with multi node weights we also have multiple imbalances
    std::vector<ValueType> imbalances;

    //MM, metrics map
    std::map<std::string,ValueType> MM = {
        {"timeMigrationAlgo",-1.0}, {"timeFirstDistribution",-1.0}, {"timeTotal",-1.0}, {"timeSpMV",-1.0}, {"timeComm",-1.0}, {"reportTime",-1.0},
        {"inputTime",-1.0}, {"timeFinalPartition",-1.0}, {"timeKmeans",-1.0}, {"timeSecondDistribution",-1.0}, {"timePreliminary",-1.0},
        {"preliminaryCut",-1.0}, {"preliminaryImbalance",-1.0}, {"finalCut",-1.0}, {"finalImbalance",-1.0}, {"maxBlockGraphDegree",-1.0},
        {"totalBlockGraphEdges",-1.0}, {"maxCommVolume",-1.0}, {"totalCommVolume",-1.0}, {"maxBoundaryNodes",-1.0}, {"totalBoundaryNodes",-1.0},
        {"maxBorderNodesPercent",-1.0}, {"avgBorderNodesPercent",-1.0},
        {"maxBlockDiameter",-1.0}, {"harmMeanDiam",-1.0}, {"numDisconBlocks",-1.0},
        {"maxRedistVol",-1.0}, {"totRedistVol",-1.0},	 //redistribution metrics
        {"maxCongestion",-1.0}, {"maxDilation",-1.0}, {"avgDilation",0.0}		//mapping metrics
    };

    //constructors
    //

    /**Constructor that is needed if we want to use the local refinement profiling vector.
    */
    Metrics( Settings settings) {
        localRefDetails.resize( settings.multiLevelRounds+1 );
        for( int i=0; i<settings.multiLevelRounds+1; i++) {
            //WARNING: problem if refinement rounds are more than 50. Very, very difficult to happen
            localRefDetails[i].resize( 50, std::make_pair(-1,-1) );
        }
    }

    /**@brief Default constructor
    */
    Metrics( ) {	}


    Metrics operator=(const Metrics &m) {
        this->MM = m.MM;
        return *this;
    }


    /** @brief Get all possible metrics.

    @param[in] graph The input graph
    @param[in] partition A partition of the graph.
    @param[in] nodeWeights The weights for the vertices of the graph.
    @param[in] settings A Settings struct.
    */
    void getAllMetrics(const scai::lama::CSRSparseMatrix<ValueType> graph, const scai::lama::DenseVector<IndexType> partition, const std::vector<scai::lama::DenseVector<ValueType>> nodeWeights, struct Settings settings );

    /** @brief Get metrics that for the max and total redistribution volume

    @param[in] graph The input graph
    @param[in] partition A partition of the graph.
    @param[in] nodeWeights The weights for the vertices of the graph.
    @param[in] settings A Settings struct.
    */
    void getRedistMetrics( const scai::lama::CSRSparseMatrix<ValueType> graph, const scai::lama::DenseVector<IndexType> partition, const std::vector<scai::lama::DenseVector<ValueType>> nodeWeights, struct Settings settings );

    /** @brief Get metrics that require some redistribution of the input data and thus are more time consuming.

    @param[in] graph The input graph
    @param[in] partition A partition of the graph.
    @param[in] nodeWeights The weights for the vertices of the graph.
    @param[in] settings A Settings struct.
    */
    void getRedistRequiredMetrics( const scai::lama::CSRSparseMatrix<ValueType> graph, const scai::lama::DenseVector<IndexType> partition, struct Settings settings, const IndexType repeatTimes );

    /** @brief Get metrics that fast to get. These are: cut, imbalance, max and total communication volume,
    max and total number of boundary nodes per block, max and total percentage of boundary nodes over all nodes.

    @param[in] graph The input graph
    @param[in] partition A partition of the graph.
    @param[in] nodeWeights The weights for the vertices of the graph.
    @param[in] settings A Settings struct.
    */
    void getEasyMetrics( const scai::lama::CSRSparseMatrix<ValueType> graph, const scai::lama::DenseVector<IndexType> partition, const std::vector<scai::lama::DenseVector<ValueType>> nodeWeights, struct Settings settings );

    /** Get the diameter of maximum diameter for all blocks. If a block is disconnected the diameter is infinite.

    @param[in] graph The input graph
    @param[in] partition A partition of the graph.
    @param[in] settings A Settings struct.

    @return first is maximum finite diameter, second is the harmonic mean of all the diameters (disconnected blocks that contribute
    an infinite diameter are taken into account), third is the number of disconnected blocks
    */
    std::tuple<IndexType,IndexType,IndexType> getDiameter( const scai::lama::CSRSparseMatrix<ValueType> graph, const scai::lama::DenseVector<IndexType> partition, struct Settings settings );

    /** Calculate the redistribution volume between to distributions, i.e., the data that will be exchanged when redistributing from oldDist to newDist.
    We calculate the redistribution volume for all blocks and return the maximum (among all blocks) and the total, i.e. the sum of all volumes.
    The two distribution must have the same size.
    @param[in] newDist The new distribution
    @param[in] oldDist The old distribution

    @return first is the maximum redistribution volume, second is the total redistribution volume.
     */
    std::pair<IndexType,IndexType> getRedistributionVol( const scai::dmemo::DistributionPtr newDist, const scai::dmemo::DistributionPtr oldDist);

    /** Get metrics concerning mapping. These are 3 metrics for: maximum congestion, maximum and average dilation

    @param[in] blockGraph The block (or communication) graph.
    @param[in] PEGraph The processor graph (or physical network).
    @param[in] mapping A mapping from blocks to PEs.
    **/
    void getMappingMetrics(
        const scai::lama::CSRSparseMatrix<ValueType> blockGraph,
        const scai::lama::CSRSparseMatrix<ValueType> PEGraph,
        const std::vector<IndexType> mapping);

    /** Given the input graph, a partition of the graph and the network, calculate the mapping metrics
    (internally, this calls getMappingMetrics). Internally, the identity mapping is assumed.
    @param[in] appGraph The application graph
    @param[in] partition A partition of the application graph. The number of blocks
    should be equal to the number of nodes of the PE graph.
    @param[in] PEGraph The processor graph (or physical network).

    **/
    void getMappingMetrics(
        const scai::lama::CSRSparseMatrix<ValueType> appGraph,
        const scai::lama::DenseVector<IndexType> partition,
        const scai::lama::CSRSparseMatrix<ValueType> PEGraph );

    //@{
    /** @name Print metrics

    Different funtions to print the metrics information
    */

    void print( std::ostream& out) const ;

    void printHorizontal( std::ostream& out ) const ;

    void printHorizontal2( std::ostream& out ) const ;

    void printKMeansProfiling( std::ostream& out ) const ;
    //@}

}; //struct Metrics


//-------------------------------------------------------------------------------------------------------------

//TODO: add default constructor and remove Settings

inline struct Metrics aggregateVectorMetrics( const std::vector<struct Metrics>& metricsVec ) {

    const scai::dmemo::CommunicatorPtr comm = scai::dmemo::Communicator::getCommunicatorPtr();

    IndexType numRuns = metricsVec.size();

    Metrics aggregateMetrics( metricsVec[0] );
    //aggregateMetrics  = metricsVec[0] ?

    for(IndexType run=0; run<numRuns; run++) {
        //this is copied because the compiler complains about the const-ness if we use a reference
        Metrics thisMetric = metricsVec[ run ];

        // for these time we have one measurement per PE and must make a max
        aggregateMetrics.MM["timeMigrationAlgo"] += comm->max( thisMetric.MM["timeMigrationAlgo"] );
        aggregateMetrics.MM["timeMigrationAlgo"] /= numRuns;
        aggregateMetrics.MM["timeFirstDistribution"] +=  comm->max( thisMetric.MM["timeFirstDistribution"] );
        aggregateMetrics.MM["timeFirstDistribution"] /= numRuns;
        aggregateMetrics.MM["timeKmeans"] += comm->max( thisMetric.MM["timeKmeans"] );
        aggregateMetrics.MM["timeKmeans"] /= numRuns;
        aggregateMetrics.MM["timeSecondDistribution"] += comm->max( thisMetric.MM["timeSecondDistribution"] );
        aggregateMetrics.MM["timeSecondDistribution"] /= numRuns;
        aggregateMetrics.MM["timePreliminary"] += comm->max( thisMetric.MM["timePreliminary"] );
        aggregateMetrics.MM["timePreliminary"] /= numRuns;

        // these times are global, no need to max, TODO: make them local and max here for consistency?
        aggregateMetrics.MM["timeFinalPartition"] += thisMetric.MM["timeFinalPartition"];
        aggregateMetrics.MM["timeFinalPartition"] /= numRuns;

        aggregateMetrics.MM["timeSpMV"] += thisMetric.MM["timeSpMV"];
        aggregateMetrics.MM["timeSpMV"] /= numRuns;
        aggregateMetrics.MM["timeComm"] += thisMetric.MM["timeComm"];
        aggregateMetrics.MM["timeComm"] /= numRuns;

        //TODO: is this too much? maybe add a shorter print?
        //thisMetric.print( out );
    }

    return aggregateMetrics;
}
//-------------------------------------------------------------------------------------------------------------

} //namespace ITI