/*
 * IO.h
 *
 *  Created on: 15.02.2017
 *      Author: moritzl
 */

#pragma once

#include <scai/lama.hpp>
#include <scai/lama/matrix/CSRSparseMatrix.hpp>
#include <scai/lama/DenseVector.hpp>

#include "quadtree/QuadTreeCartesianEuclid.h"

using scai::lama::CSRSparseMatrix;
using scai::lama::DenseVector;

#include <vector>
#include <set>
#include <memory>

#define PRINT( msg ) std::cout<< __FILE__<< ", "<< __LINE__ << ": "<< msg << std::endl
#define PRINT0( msg ) if(comm->getRank()==0)  std::cout<< __FILE__<< ", "<< __LINE__ << ": "<< msg << std::endl

namespace ITI {
        
template <typename IndexType, typename ValueType>
class FileIO {

public:
        /** METIS format: for graphs: first line are the nodes, N, and edges, E, of the graph
         *                            the next N lines contain the neighbours for every node. 
         *                            So if line 100 is "120 1234 8 2133" means that node 100
         *                            has edges to nodes 120, 1234, 8 and 2133.
         *                for coordinates (up to 3D): every line has 3 numbers, the real valued
         *                            coordiantes. If the coordinates are in 2D the last number is 0.
         * MATRIXMARKET format: for graphs: we use the function readFromFile (or readFromSingleFile) 
         *                            provided by LAMA.
         *                for coordiantes: first line has two numbers, the number of points N and
         *                            the dimension d. Then next N*d lines contain the coordinates
         *                            for the poitns: every d lines are the coordinates for a point.
        */
        enum class FileFormat{ METIS = 0 , MATRIXMARKET = 1};

	/** Given an adjacency matrix and a filename writes the matrix in the file using the METIS format.
	 *
	 * @param[in] adjM The graph's adjacency matrix. 
	 * @param[in] filename The file's name to write to
	 */
	static void writeGraph (const CSRSparseMatrix<ValueType> &adjM, const std::string filename);

	/** Given an adjacency matrix and a filename writes the local part of matrix in the file using the METIS format.
	 *  Every proccesor adds his rank in the end of the file name.
	 * @param[in] adjM The graph's adjacency matrix.
	 * @param[in] filename The file's name to write to
	 */
	static void writeGraphDistributed (const CSRSparseMatrix<ValueType> &adjM, const std::string filename);

	/** Given the vector of the coordinates and their dimension, writes them in file "filename".
	 * Coordinates are given as a DenseVector of size dim*numPoints.
	*/
	static void writeCoords (const std::vector<DenseVector<ValueType>> &coords, const std::string filename);

        static void writeCoordsDistributed_2D (const std::vector<DenseVector<ValueType>> &coords, IndexType numPoints, const std::string filename);

        /** Writes a partition to file.
	 * @param[in] part The partition to br written.
	 * @param[in] filename The file's name to write to.
	 */
	static void writePartition(const DenseVector<IndexType> &part, const std::string filename);
        
	/** Reads a graph from filename in METIS format and returns the adjacency matrix.
	 * @param[in] filename The file to read from.
         * @param[in] fileFormat The type of file to read from. 
	 * @return The adjacency matrix of the graph. The rows of the matrix are distributed with a BlockDistribution and NoDistribution for the columns.
	 */
	static CSRSparseMatrix<ValueType> readGraph(const std::string filename , const FileFormat fileFormat=FileFormat::METIS);

	/** Reads the 2D coordinates from file "filename" and returns then in a DenseVector where the coordinates of point i are in [i*2][i*2+1].
	 */
	static std::vector<DenseVector<ValueType>> readCoords ( const std::string filename, const IndexType numberOfCoords, const IndexType dimension, const FileFormat fileFormat=FileFormat::METIS);

	/**
	 * Reads a partition from file.
	 */
	static DenseVector<IndexType> readPartition(const std::string filename);

	/**
	 * Reads a quadtree as specified in the format of Michael Selzer
	 */
	static CSRSparseMatrix<ValueType> readQuadTree( std::string filename, std::vector<DenseVector<ValueType>> &coords);

	/**
	 * Reads a quadtree as specified in the format of Michael Selzer
	 */
	static CSRSparseMatrix<ValueType> readQuadTree( std::string filename) {
		std::vector<DenseVector<ValueType>> coords;
		return readQuadTree(filename, coords);
	}

        static std::pair<IndexType, IndexType> getMatrixMarketCoordsInfos(const std::string filename);

private:
	/**
	 * given the central coordinates of a cell and its level, compute the bounding corners
	 */
	static std::pair<std::vector<ValueType>, std::vector<ValueType>> getBoundingCoords(std::vector<ValueType> centralCoords, IndexType level);
        
        /** Reads the coordinates for the MatrixMarket file format.
         */
        static std::vector<DenseVector<ValueType>> readCoordsMatrixMarket ( const std::string filename);
};

} /* namespace ITI */
