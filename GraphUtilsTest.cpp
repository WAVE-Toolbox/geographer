#include <scai/lama.hpp>

#include "gtest/gtest.h"

#include "ParcoRepart.h"
#include "FileIO.h"
#include "GraphUtils.h"

namespace ITI {

class GraphUtilsTest : public ::testing::Test {
    protected:
        // the directory of all the meshes used
        std::string graphPath = "./meshes/";
};

TEST_F(GraphUtilsTest, testReindexCut){
    std::string fileName = "trace-00008.graph";
    std::string file = graphPath + fileName;
    
    IndexType dimensions= 2;
    CSRSparseMatrix<ValueType> graph = FileIO<IndexType, ValueType>::readGraph(file );
    const IndexType n = graph.getNumRows();

    const scai::dmemo::DistributionPtr dist = graph.getRowDistributionPtr();
    const scai::dmemo::CommunicatorPtr comm = dist->getCommunicatorPtr();
    // for now local refinement requires k = P
    const IndexType k = comm->getSize();
    
    std::vector<DenseVector<ValueType>> coords = FileIO<IndexType, ValueType>::readCoords( std::string(file + ".xyz"), n, dimensions);
    ASSERT_TRUE(coords[0].getDistributionPtr()->isEqual(*dist));

    //get sfc partition
    Settings settings;
    settings.numBlocks = k;
    settings.noRefinement = true;
    DenseVector<IndexType> partition = ParcoRepart<IndexType, ValueType>::partitionGraph(graph, coords, settings);
	
	//WARNING: with the noRefinement flag the partition is not destributed
	partition.redistribute( dist);
	
	ASSERT_TRUE( coords[0].getDistributionPtr()->isEqual(*dist) );
	ASSERT_TRUE( partition.getDistributionPtr()->isEqual(*dist) );
    //scai::dmemo::DistributionPtr noDistPointer(new scai::dmemo::NoDistribution(n));    

    //get first cut
    ValueType initialCut = GraphUtils::computeCut<IndexType, ValueType>(graph, partition, true);
    ASSERT_GE(initialCut, 0);
    ValueType sumNonLocalInitial = ParcoRepart<IndexType, ValueType>::localSumOutgoingEdges(graph, true);

	PRINT("about to reindex the graph");
    //now reindex and get second cut
    GraphUtils::reindex<IndexType, ValueType>(graph);
    ValueType sumNonLocalAfterReindexing = ParcoRepart<IndexType, ValueType>::localSumOutgoingEdges(graph, true);
    EXPECT_EQ(sumNonLocalInitial, sumNonLocalAfterReindexing);

    DenseVector<IndexType> reIndexedPartition = DenseVector<IndexType>(graph.getRowDistributionPtr(), partition.getLocalValues());
    ASSERT_TRUE(reIndexedPartition.getDistributionPtr()->isEqual(*graph.getRowDistributionPtr()));

    ValueType secondCut = GraphUtils::computeCut<IndexType, ValueType>(graph, reIndexedPartition, true);

    EXPECT_EQ(initialCut, secondCut);
}
//--------------------------------------------------------------------------------------- 

} //namespace
