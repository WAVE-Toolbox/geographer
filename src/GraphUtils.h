/*
 * GraphUtils.h
 *
 *  Created on: 29.06.2017
 *      Authors: Moritz von Looz, Charilaos Tzovas
 */

#pragma once

#include <set>
#include <tuple>

#include <scai/lama/matrix/CSRSparseMatrix.hpp>
#include <scai/dmemo/Distribution.hpp>
#include <scai/dmemo/BlockDistribution.hpp>
#include <scai/dmemo/GeneralDistribution.hpp>

#include <boost/config.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/betweenness_centrality.hpp>
#include <boost/graph/properties.hpp>

#include "Settings.h"


namespace ITI {

//namespace GraphUtils {
template <typename IndexType, typename ValueType>
class GraphUtils {
public:
/**
 * Reindexes the nodes of the input graph to form a BlockDistribution. No redistribution of the graph happens, only the indices are changed.
 * After this method is run, the input graph has a BlockDistribution.
 *
 * @param[in,out] the graph
 *
 * @return A block-distributed vector containing the old indices
 */
//template<typename IndexType, typename ValueType>
static scai::lama::DenseVector<IndexType> reindex(scai::lama::CSRSparseMatrix<ValueType> &graph);

/**
 * @brief Perform a BFS on the local subgraph.
 *
 * @param[in] graph (may be distributed)
 * @param[in] u local index of starting node
 *
 * @return vector with (local) distance to u for each local node
 */
//template<typename IndexType, typename ValueType>
static std::vector<IndexType> localBFS(const scai::lama::CSRSparseMatrix<ValueType> &graph, IndexType u);

/**
	@brief Single source shortest path, a Dijkstra implementation

	* @param[in] graph (may be distributed)
	* @param[in] u local index of starting node
	* @param[out] predecessor A vector with the predecessor of a node in 
	the shortest path. 
	Example, predecessor[4] = 5 means that in the shortes path from u to 4,
	the previous vertex is 5. If also, predeccor[5] = 10 and predecessor[10] = u
	then the shortes path to 4 is: u--10--5--4

	* @return vector with (local) distance to u for each local node
*/

//template<typename IndexType, typename ValueType>
static std::vector<ValueType> localDijkstra(const scai::lama::CSRSparseMatrix<ValueType> &graph, const IndexType u, std::vector<IndexType>& predecessor);
/**
 * @brief Computes the diameter of the local subgraph using the iFUB algorithm.
 *
 * @param graph
 * @param u local index of starting node. Should be central.
 * @param lowerBound of diameter. Can be 0. A good lower bound might speed up the computation
 * @param k tolerance Algorithm aborts if upperBound - lowerBound <= k
 * @param maxRounds Maximum number of diameter rounds.
 *
 * @return new lower bound
 */

//template<typename IndexType, typename ValueType>
static IndexType getLocalBlockDiameter(const scai::lama::CSRSparseMatrix<ValueType> &graph, const IndexType u, IndexType lowerBound, const IndexType k, IndexType maxRounds);

/**
 * This method takes a (possibly distributed) partition and computes its global cut.
 *
 * @param[in] input The adjacency matrix of the graph.
 * @param[in] part The partition vector for the input graph.
 * @param[in] weighted If edges are weighted or not.
 */
//template<typename IndexType, typename ValueType>
static ValueType computeCut(const scai::lama::CSRSparseMatrix<ValueType> &input, const scai::lama::DenseVector<IndexType> &part, bool weighted = false);

/**
 * This method takes a (possibly distributed) partition and computes its imbalance.
 * The number of blocks is also a required input, since it cannot be guessed accurately from the partition vector if a block is empty.
 *
 * @param[in] part partition
 * @param[in] k number of blocks in partition.
 * @param[in] nodeWeights The weight of every point/vertex if available
 * @param[in] blockSizes The optimum size/weight for every block
 */

//TODO: this does not include the case where we can have different
//blocks sizes but no node weights; adapt

//template<typename IndexType, typename ValueType>
static ValueType computeImbalance(
	const scai::lama::DenseVector<IndexType> &part,
	IndexType k,
	const scai::lama::DenseVector<ValueType> &nodeWeights = scai::lama::DenseVector<ValueType>(0,0),
	const std::vector<ValueType> &blockSizes = std::vector<ValueType>(0,0));

/**
 * @brief Builds a halo containing all non-local neighbors.
 *
 * @param[in] input Adjacency Matrix
 *
 * @return HaloExchangePlan
 */
//template<typename IndexType, typename ValueType>
static scai::dmemo::HaloExchangePlan buildNeighborHalo(const scai::lama::CSRSparseMatrix<ValueType> &input);

/**
 * Returns true if the node identified with globalID has a neighbor that is not local on this process.
 * Since this method acquires reading locks on the CSR structure, it might be expensive to call often
 * 
 * @param[in] input The adjacency matrix of the graph.
 * @param[in] globalID The global ID of the vertex to be checked
 * @return True if the vertex has a neighbor that resides in a differetn PE, false if all the neighbors are local.
 * 
 */
//template<typename IndexType, typename ValueType>
static bool hasNonLocalNeighbors(const scai::lama::CSRSparseMatrix<ValueType> &input, IndexType globalID);

/**
 * Returns a vector of global indices of nodes which are local on this process, but have neighbors that are not local.
 * These non-local neighbors may or may not be in the same block.
 * No communication required, iterates once over the local adjacency matrix
 * @param[in] input Adjacency matrix of the input graph
 */
//template<typename IndexType, typename ValueType>
static  std::vector<IndexType> getNodesWithNonLocalNeighbors(const scai::lama::CSRSparseMatrix<ValueType>& input);

/**
 * Returns a vector of global indices of nodes which are local on this process, but have neighbors that are not local.
 * This method differs from the other method with the same name by accepting a list of candidates.
 * Only those are checked for non-local neighbors speeding up the process.
 *
 * @param[in] input Adjacency matrix of the input graph
 * @param[in]
 *
 * @return vector of nodes with non-local neighbors
 */
//template<typename IndexType, typename ValueType>
static std::vector<IndexType> getNodesWithNonLocalNeighbors(const scai::lama::CSRSparseMatrix<ValueType>& input, const std::set<IndexType>& candidates);

/**
 * Computes a list of global IDs of nodes which are adjacent to nodes local on this processor, but are themselves not local.
 * @param[in] input Adjacency matrix of the input graph
 * @return The vector with the global IDs 
 */
//template<typename IndexType, typename ValueType>
static std::vector<IndexType> nonLocalNeighbors(const scai::lama::CSRSparseMatrix<ValueType>& input);

/** Get the borders nodes of each block. Border node: one that has at least one neighbor in different block.
*/
//template<typename IndexType, typename ValueType>
static scai::lama::DenseVector<IndexType> getBorderNodes( const scai::lama::CSRSparseMatrix<ValueType> &adjM, const scai::lama::DenseVector<IndexType> &part);

/* Returns two vectors each of size k: the first contains the number of border nodes per block and the second one the number of inner nodes per blocks.
 *
 */
//template<typename IndexType, typename ValueType>
static std::pair<std::vector<IndexType>,std::vector<IndexType>> getNumBorderInnerNodes( const scai::lama::CSRSparseMatrix<ValueType> &adjM, const scai::lama::DenseVector<IndexType> &part, Settings settings);

/* Computes the communication volume for every block. 
 * TODO: Should the result is gathered in the root PE and not be replicated?
 * */
//template<typename IndexType, typename ValueType>
static std::vector<IndexType> computeCommVolume( const scai::lama::CSRSparseMatrix<ValueType> &adjM, const scai::lama::DenseVector<IndexType> &part, Settings settings );

/**Computes the communication volume, boundary and inner nodes in one pass to save time.
 * 
 * @return A tuple with three vectors each od size numBlocks: first vector is the communication volume, second is the number of boundary nodes and third
 * the number of inner nodes pre block.
 */
//template<typename IndexType, typename ValueType>
static std::tuple<std::vector<IndexType>, std::vector<IndexType>, std::vector<IndexType>> computeCommBndInner( const scai::lama::CSRSparseMatrix<ValueType> &adjM, const scai::lama::DenseVector<IndexType> &part, Settings settings );


/** Builds the block (aka, communication) graph of the given partition.
 * Creates an HArray that is passed around in numPEs (=comm->getSize()) rounds and every time
 * a processor writes in the array its part.
 *
 * The returned matrix is replicated in all PEs.
 *
 * @param[in] adjM The adjacency matric of the input graph.
 * @param[in] part The partition of the input garph.
 * @param[in] k Number of blocks.
 *
 * @return The adjacency matrix of the block graph.
 */
//template<typename IndexType, typename ValueType>
static scai::lama::CSRSparseMatrix<ValueType> getBlockGraph( const scai::lama::CSRSparseMatrix<ValueType> &adjM, const scai::lama::DenseVector<IndexType> &part, const IndexType k);

static scai::lama::CSRSparseMatrix<ValueType>  getBlockGraph_dist( const scai::lama::CSRSparseMatrix<ValueType> &adjM, const scai::lama::DenseVector<IndexType> &part, const IndexType k);


/** Get the maximum degree of a graph.
 * */
//template<typename IndexType, typename ValueType>
static IndexType getGraphMaxDegree( const scai::lama::CSRSparseMatrix<ValueType>& adjM);


/** first = Compute maximum communication = max degree of the block graph.
 *  second = Compute total communication = sum of all edges of the block graph.
 */
//template<typename IndexType, typename ValueType>
static std::pair<IndexType, IndexType> computeBlockGraphComm( const scai::lama::CSRSparseMatrix<ValueType>& adjM, const scai::lama::DenseVector<IndexType> &part, const IndexType k);

/** Compute maximum and total communication volume= max and sum of number of border nodes
 * WARNING: this works properly only when k=p and the nodes are redistributed so each PE owns nodes from one block.
 */
//template<typename IndexType, typename ValueType>
static std::pair<IndexType,IndexType> computeCommVolume_p_equals_k( const scai::lama::CSRSparseMatrix<ValueType>& adjM, const scai::lama::DenseVector<IndexType> &part);
    
/**Returns the process graph. Every processor traverses its local part of adjM and for every
 * edge (u,v) that one node, say u, is not local it gets the owner processor of u.
 * The returned graph is distributed with a BLOCK distribution where each PE owns one row.
 *
 * @param[in] adjM The adjacency matrix of the input graph.
 * @return A [#PE x #PE] adjacency matrix of the process graph, distributed with a Block distribution
 */
//template<typename IndexType, typename ValueType>
static scai::lama::CSRSparseMatrix<ValueType> getPEGraph( const scai::lama::CSRSparseMatrix<ValueType> &adjM);

/**Returns the process graph, as calculated from the local halos.
 * The edges of the process graph has weights that indicate the number of vertices (not edges) that
 * are not local. eg: w(0,1)=10 means than PE 0 has 10 neighboring vertices in PE 1
 * and if w(1,0)=12, then PE 1 has 12 neighboring vertices in PE 0. The graph is not symmetric.
 *
 * @param halo HaloExchangePlan objects in which all non-local neighbors are present
 * @return A [#PE x #PE] adjacency matrix of the process graph, distributed with a Block distribution
 */
//template<typename IndexType, typename ValueType>
static scai::lama::CSRSparseMatrix<ValueType> getPEGraph( const scai::dmemo::HaloExchangePlan& halo);

/**
 * @Convert a set of unweighted adjacency lists into a CSR matrix
 *
 * @param[in] adjList For each node, a possibly empty set of neighbors
 * @return The distributed adjacency matrix
 */
//template<typename IndexType, typename ValueType>
static scai::lama::CSRSparseMatrix<ValueType> getCSRmatrixFromAdjList_NoEgdeWeights( const std::vector<std::set<IndexType>>& adjList);

/** @brief Get a vector of the local edges, sort the edges and construct the local part of CSR sparse matrix.
 *
 * @param[in] edgeList The local list of edges for this PE.
 * @return The distributed adjacency matrix.
 */
//template<typename IndexType, typename ValueType>
static scai::lama::CSRSparseMatrix<ValueType> edgeList2CSR( std::vector< std::pair<IndexType, IndexType>>& edgeList );


/*	@brief Given a csr sparse matrix, it calulates its edge list representations.
		For every tuple, the first two numbers are the vertex IDs for this edge and the
		third is the edge weight.
	@param[in] graph The input graph (ignores direction)
	@param[out] maxDegree The maximum degree of the graph
	@return The edge list representation. Size is equal to graph.getNumValues()/2.
*/
//WARNING,TODO: Assumes the graph is undirected
//template<typename IndexType, typename ValueType>
static std::vector<std::tuple<IndexType,IndexType,ValueType>> CSR2EdgeList_local(const scai::lama::CSRSparseMatrix<ValueType>& graph, IndexType& maxDegree=0);

/**
 * @brief Construct the Laplacian of the input matrix. May contain parallel communication.
 *
 * @param graph Input matrix, must have a (general) block distribution or be replicated.
 *
 * @return laplacian with same distribution as input
 */
//template<typename IndexType, typename ValueType>
static scai::lama::CSRSparseMatrix<ValueType> constructLaplacian(const scai::lama::CSRSparseMatrix<ValueType>& graph);

/**
 * @brief Construct a replicated projection matrix for a fast Johnson-Lindenstrauß-Transform
 *
 * @param epsilon Desired accuracy of transform
 * @param n
 * @param origDimension Dimension of original space
 *
 * @return FJLT matrix
 */
//template<typename IndexType, typename ValueType>
static scai::lama::CSRSparseMatrix<ValueType> constructFJLTMatrix(ValueType epsilon, IndexType n, IndexType origDimension);

/**
 * @brief Construct a replicated Hadamard matrix
 *
 * @param d Dimension
 *
 * @return Hadamard matrix
 */
//template<typename IndexType, typename ValueType>
static scai::lama::DenseMatrix<ValueType> constructHadamardMatrix(IndexType d);

//taken from https://stackoverflow.com/a/9345144/494085

//template<typename IndexType, typename ValueType>
static std::vector< std::vector<IndexType>> mecGraphColoring( const scai::lama::CSRSparseMatrix<ValueType> &graph, IndexType &colors);

/**
 * @brief Randomly select elements and move them to the front.
 *
 * @param begin Begin of range
 * @param end End of range
 * @param num_random Number of selected elements
 *
 */
template<class BidiIter >
static BidiIter FisherYatesShuffle(BidiIter begin, BidiIter end, size_t num_random) {
    size_t left = std::distance(begin, end);
    for (IndexType i = 0; i < num_random; i++) {
        BidiIter r = begin;
        std::advance(r, rand()%left);
        std::swap(*begin, *r);
        ++begin;
        --left;
    }
    return begin;
}

/**
	@brief Get the betweenness centality of all nodes of given graph
	using boost::brandes_betweenness_centrality

*/
//template<typename IndexType, typename ValueType>
static std::vector<ValueType> getBetweennessCentrality(const scai::lama::CSRSparseMatrix<ValueType>& graph, bool normalize= false);


/**	Reordering a sequence of numbers from 0 to maxIndex.
 * The order is: maxIndex/2, maxIdnex/4, maxIndex*3/4, maxIndex/8, maxIndex*3/8, maxIndex*5/8, ...
 * @return The premutated numbers. return.size()=maxIdnex and 0< return[i]< maxIndex.
 */


//TODO: verify that it works properly

static std::vector<IndexType> indexReorderCantor(const IndexType maxIndex);



}; //class GraphUtils
//} /*namespace GraphUtils*/

} /* namespace ITI */