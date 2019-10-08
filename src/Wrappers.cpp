/*
 * Wrappers.cpp
 *
 *  Created on: 02.02.2018
 *      Author: tzovas
 */

#include <parmetis.h>

#include <scai/partitioning/Partitioning.hpp>

//for zoltan
//#include <Zoltan2_PartitioningSolution.hpp>
//#include <Zoltan2_PartitioningProblem.hpp>
//#include <Zoltan2_BasicVectorAdapter.hpp>
//#include <Zoltan2_InputTraits.hpp>


#include "Wrappers.h"
#include "Mapping.h"
#include "AuxiliaryFunctions.h"


namespace ITI {

IndexType HARD_TIME_LIMIT= 600; 	// hard limit in seconds to stop execution if exceeded
//using ValueType= real_t;

template<typename IndexType, typename ValueType>
scai::lama::DenseVector<IndexType> Wrappers<IndexType, ValueType>::partition(
    const scai::lama::CSRSparseMatrix<ValueType> &graph,
    const std::vector<scai::lama::DenseVector<ValueType>> &coordinates,
    const std::vector<scai::lama::DenseVector<ValueType>> &nodeWeights,
    bool nodeWeightsFlag,
    Tool tool,
    struct Settings &settings,
    Metrics<ValueType> &metrics	) {

    scai::lama::DenseVector<IndexType> partition;
    switch( tool) {
    case Tool::parMetisGraph:
        partition = metisPartition( graph, coordinates, nodeWeights, nodeWeightsFlag, 0, settings, metrics);
        break;
    case Tool::parMetisGeom:
        partition =  metisPartition( graph, coordinates, nodeWeights, nodeWeightsFlag, 1, settings, metrics);
        break;
    case Tool::parMetisSFC:
        partition = metisPartition( graph, coordinates, nodeWeights, nodeWeightsFlag, 2, settings, metrics);
        break;
    case Tool::zoltanRIB:
        partition = zoltanPartition( graph, coordinates, nodeWeights, nodeWeightsFlag, "rib", settings, metrics);
        break;
    case Tool::zoltanRCB:
        partition = zoltanPartition( graph, coordinates, nodeWeights, nodeWeightsFlag, "rcb", settings, metrics);
        break;
    case Tool::zoltanMJ:
        partition = zoltanPartition( graph, coordinates, nodeWeights, nodeWeightsFlag, "multijagged", settings, metrics);
        break;
    case Tool::zoltanSFC:
        partition = zoltanPartition( graph, coordinates, nodeWeights, nodeWeightsFlag, "hsfc", settings, metrics);
        break;
    default:
        throw std::runtime_error("Wrong tool given to partition.\nAborting...");
        partition = scai::lama::DenseVector<IndexType>(graph.getLocalNumRows(), -1 );
    }

    if( settings.mappingRenumbering ) {
        const scai::dmemo::CommunicatorPtr comm = scai::dmemo::Communicator::getCommunicatorPtr();
        PRINT0("Applying renumbering of blocks based on the SFC index of their centers.");
        std::chrono::time_point<std::chrono::system_clock> startRnb = std::chrono::system_clock::now();

        Mapping<IndexType,ValueType>::applySfcRenumber( coordinates, nodeWeights, partition, settings );

        std::chrono::duration<double> elapTime = std::chrono::system_clock::now() - startRnb;
        PRINT0("renumbering time " << elapTime.count() );
    }

    return partition;
}
//-----------------------------------------------------------------------------------------
template<typename IndexType, typename ValueType>
scai::lama::DenseVector<IndexType> Wrappers<IndexType, ValueType>::partition(
    const std::vector<scai::lama::DenseVector<ValueType>> &coordinates,
    const std::vector<scai::lama::DenseVector<ValueType>> &nodeWeights,
    bool nodeWeightsFlag,
    Tool tool,
    struct Settings &settings,
    Metrics<ValueType> &metrics	) {

    //create dummy graph as the these tools do not use it.
    const scai::dmemo::DistributionPtr distPtr = coordinates[0].getDistributionPtr();
    const scai::dmemo::DistributionPtr noDistPtr( new scai::dmemo::NoDistribution(distPtr->getGlobalSize()) );
    const scai::lama::CSRSparseMatrix<ValueType> graph = scai::lama::zero<CSRSparseMatrix<ValueType>>( distPtr, noDistPtr );

    scai::lama::DenseVector<IndexType> retPart;
    switch( tool ) {
    case Tool::parMetisGraph:
    case Tool::parMetisGeom:
        PRINT("Tool "<< tool <<" requires the graph to compute a partition but no graph was given.");
        throw std::runtime_error("Missing graph.\nAborting...");
        break;
    case Tool::parMetisSFC:
    case Tool::zoltanRIB:
    case Tool::zoltanRCB:
    case Tool::zoltanMJ:
    case Tool::zoltanSFC:
        //call partition function
        retPart =  partition( graph, coordinates, nodeWeights, nodeWeightsFlag, tool, settings, metrics);
        break;
    default:
        throw std::runtime_error("Wrong tool given to partition.\nAborting...");
        retPart = scai::lama::DenseVector<IndexType>(graph.getLocalNumRows(), -1 );
    }//switch

    return retPart;
}
//-----------------------------------------------------------------------------------------

template<typename IndexType, typename ValueType>
scai::lama::DenseVector<IndexType> Wrappers<IndexType, ValueType>::repartition (
    const scai::lama::CSRSparseMatrix<ValueType> &graph,
    const std::vector<scai::lama::DenseVector<ValueType>> &coordinates,
    const std::vector<scai::lama::DenseVector<ValueType>> &nodeWeights,
    bool nodeWeightsFlag,
    Tool tool,
    struct Settings &settings,
    Metrics<ValueType> &metrics) {

    switch( tool) {
    // for repartition, metis uses the same function
    case Tool::parMetisGraph:
    case Tool::parMetisGeom:
    case Tool::parMetisSFC:
        throw std::runtime_error("Unfortunatelly, Current version does not support repartitioning with parmetis.\nAborting...");
    //TODO: parmetis needs consective indices for the vertices; must reindex vertices
    //return metisRepartition( graph, coordinates, nodeWeights, nodeWeightsFlag, settings, metrics);

    case Tool::zoltanRIB:
        return zoltanPartition( graph, coordinates, nodeWeights, nodeWeightsFlag, "rib", settings, metrics);

    case Tool::zoltanRCB:
        return zoltanRepartition( graph, coordinates, nodeWeights, nodeWeightsFlag, "rcb", settings, metrics);

    case Tool::zoltanMJ:
        return zoltanRepartition( graph, coordinates, nodeWeights, nodeWeightsFlag, "multijagged", settings, metrics);

    case Tool::zoltanSFC:
        return zoltanRepartition( graph, coordinates, nodeWeights, nodeWeightsFlag, "hsfc", settings, metrics);

    default:
        throw std::runtime_error("Wrong tool given to repartition.\nAborting...");
        return scai::lama::DenseVector<IndexType>(graph.getLocalNumRows(), -1 );
    }
}
//-----------------------------------------------------------------------------------------

template<typename IndexType, typename ValueType>
scai::lama::DenseVector<IndexType> Wrappers<IndexType, ValueType>::refine(
        const scai::lama::CSRSparseMatrix<ValueType> &graph,
        const std::vector<scai::lama::DenseVector<ValueType>> &coords,
        const std::vector<scai::lama::DenseVector<ValueType>> &nodeWeights,
        const scai::lama::DenseVector<IndexType> partition,
        struct Settings &settings,
        Metrics<ValueType> &metrics
    ){

    //probably we gonna have problems if the distribution does not have 
    //a consecutive numbering. Fix here or outside
    if( not std::is_same<ValueType,real_t>::value ){
        PRINT("*** Warning, ValueType and real_t do not agree");
    }

    if( sizeof(ValueType)!=sizeof(real_t) ) {
        std::cout<< "WARNING: IndexType size= " << sizeof(IndexType) << " and idx_t size=" << sizeof(idx_t) << "  do not agree, this may cause problems " << std::endl;
    }
    

    SCAI_ASSERT_DEBUG( graph.isConsistent(), graph << " input graph is not consistent" );
    //const scai::dmemo::DistributionPtr graphDist = graph.getRowDistributionPtr();

    // vtxDist is an array of size numPEs and is replicated in every processor
    std::vector<IndexType> vtxDist; 

    std::vector<IndexType> xadj;
    std::vector<IndexType> adjncy;
    // vwgt , adjwgt stores the weights of vertices.
    std::vector<ValueType> vVwgt;

    // tpwgts: array that is used to specify the fraction of
    // vertex weight that should be distributed to each sub-domain for each balance constraint.
    // Here we want equal sizes, so every value is 1/nparts; size = ncons*nparts 
    std::vector<ValueType> tpwgts;

    // the xyz array for coordinates of size dim*localN contains the local coords
    std::vector<ValueType> xyzLocal;    

    // ubvec: array of size ncon to specify imbalance for every vertex weigth.
    // 1 is perfect balance and nparts perfect imbalance. Here 1 for now
    std::vector<ValueType> ubvec;

    //local number of edges; number of node weights; flag about edge and vertex weights 
    IndexType numWeights=0, wgtFlag=0;

    // options: array of integers for passing arguments.
    std::vector<IndexType> options;

    aux<IndexType,ValueType>::toMetisInterface(
        graph, coords, nodeWeights, settings, vtxDist, xadj, adjncy,
        vVwgt, tpwgts, wgtFlag, numWeights, ubvec, xyzLocal, options );

    SCAI_ASSERT_EQ_ERROR( tpwgts.size(), numWeights*settings.numBlocks, "Wrong tpwgts size" );
    {
        scai::dmemo::CommunicatorPtr comm = scai::dmemo::Communicator::getCommunicatorPtr();
        SCAI_ASSERT_EQ_ERROR( vtxDist.size(), comm->getSize()+1, "Wrong vtxDist size" );
    }

    // nparts: the number of parts to partition (=k)
    IndexType nparts= settings.numBlocks;


    // numflag: 0 for C-style (start from 0), 1 for Fortran-style (start from 1)
    IndexType numflag= 0;          
    // edges weights not supported
    IndexType* adjwgt= NULL;

    // output parameters
    //
    // edgecut: the size of cut
    IndexType edgecut;

    const scai::dmemo::CommunicatorPtr comm = scai::dmemo::Communicator::getCommunicatorPtr();
    const scai::dmemo::DistributionPtr dist = graph.getRowDistributionPtr();
    //const IndexType N = graph.getNumRows();
    const IndexType localN= dist->getLocalSize();   

    //parmetis requires weights to be integers
    std::vector<IndexType> vwgt( vVwgt.begin(), vVwgt.end() );
    SCAI_ASSERT_EQ_ERROR( vwgt.size(), localN*numWeights, "Wrong weights size" );
    
    //partition and the graph rows must have the same distribution
    SCAI_ASSERT( dist->isEqual( partition.getDistribution()), "Distributions must agree" );
    
    // partition array of size localN, contains the block every vertex belongs
    std::vector<idx_t> partKway( localN );
    scai::hmemo::ReadAccess<IndexType> rLocalPart( partition.getLocalValues() );
    SCAI_ASSERT_EQ_ERROR( rLocalPart.size(), localN , "Wrong partition size" );

    for(int i=0; i<localN; i++){
        partKway[i]= rLocalPart[i];
    }    
    rLocalPart.release();

    // comm: the MPI communicator
    MPI_Comm metisComm;
    MPI_Comm_dup(MPI_COMM_WORLD, &metisComm);
    //int metisRet;    

    PRINT0("About to call ParMETIS_V3_RefineKway in Wrappers::refine");

    //overwrite the default options because parmetis by default neglects the
    //partition if k=p
    std::vector<IndexType>options2(4,1);
    options2[1] = 0; //verbosity
    options2[3] = PARMETIS_PSR_UNCOUPLED; //if k=p (coupled) or not (uncoupled); 2 is always uncoupled

    std::chrono::time_point<std::chrono::system_clock> startTime =  std::chrono::system_clock::now();

    ParMETIS_V3_RefineKway(
        vtxDist.data(), xadj.data(), adjncy.data(), vwgt.data(), adjwgt, &wgtFlag, &numflag, &numWeights, &nparts, tpwgts.data() , ubvec.data(), options2.data(), &edgecut, partKway.data(), &metisComm );

    std::chrono::duration<double> partitionKwayTime = std::chrono::system_clock::now() - startTime;
    double partKwayTime= comm->max(partitionKwayTime.count() );
    metrics.MM["timeFinalPartition"] = partKwayTime;

    //
    // convert partition to a DenseVector
    //

    scai::lama::DenseVector<IndexType> partitionKway(dist, scai::hmemo::HArray<IndexType>(localN, partKway.data()) );

    return partitionKway;
}
//-----------------------------------------------------------------------------------------

template<typename IndexType, typename ValueType>
scai::lama::DenseVector<IndexType> Wrappers<IndexType, ValueType>::metisPartition (
    const scai::lama::CSRSparseMatrix<ValueType> &graph,
    const std::vector<scai::lama::DenseVector<ValueType>> &coords,
    const std::vector<scai::lama::DenseVector<ValueType>> &nodeWeights,
    bool nodeWeightsFlag,
    int parMetisGeom,
    struct Settings &settings,
    Metrics<ValueType> &metrics) {

    const scai::dmemo::CommunicatorPtr comm = scai::dmemo::Communicator::getCommunicatorPtr();
    const scai::dmemo::DistributionPtr dist = graph.getRowDistributionPtr();
    //const IndexType N = graph.getNumRows();
    const IndexType localN= dist->getLocalSize();

    PRINT0("\t\tStarting the metis wrapper");

    if( comm->getRank()==0 ) {
        std::cout << "\033[1;31m";
        std::cout << "IndexType size: " << sizeof(IndexType) << " , ValueType size: "<< sizeof(ValueType) << std::endl;
        if( int(sizeof(IndexType)) != int(sizeof(idx_t)) ) {
            std::cout<< "WARNING: IndexType size= " << sizeof(IndexType) << " and idx_t size=" << sizeof(idx_t) << "  do not agree, this may cause problems (even if this print looks OK)." << std::endl;
        }
        if( sizeof(ValueType)!=sizeof(real_t) ) {
            std::cout<< "WARNING: IndexType size= " << sizeof(IndexType) << " and idx_t size=" << sizeof(idx_t) << "  do not agree, this may cause problems " << std::endl;
        }
        std::cout<<"\033[0m";
    }


    //-----------------------------------------------------
    //
    // convert to parMetis data types
    //

    double sumKwayTime = 0.0;
    int repeatTimes = settings.repeatTimes;

    //
    // parmetis partition
    //
    if( parMetisGeom==0 and comm->getRank()==0 ) std::cout<< "About to call ParMETIS_V3_PartKway" << std::endl;
    if( parMetisGeom==1 and comm->getRank()==0 ) std::cout<< "About to call ParMETIS_V3_PartGeom" << std::endl;
    if( parMetisGeom==2 and comm->getRank()==0 ) std::cout<< "About to call ParMETIS_V3_PartSfc" << std::endl;

    // partition array of size localN, contains the block every vertex belongs
    idx_t *partKway = new idx_t[ localN ];


    int r;
    for( r=0; r<repeatTimes; r++) {

        // vtxDist is an array of size numPEs and is replicated in every processor
        std::vector<IndexType> vtxDist;

        std::vector<IndexType> xadj;
        std::vector<IndexType> adjncy;
        // vwgt , adjwgt stores the weights of vertices.
        std::vector<ValueType> vVwgt;

        // tpwgts: array that is used to specify the fraction of
        // vertex weight that should be distributed to each sub-domain for each balance constraint.
        // Here we want equal sizes, so every value is 1/nparts; size = ncons*nparts 
        std::vector<ValueType> tpwgts;

        // the xyz array for coordinates of size dim*localN contains the local coords
        std::vector<ValueType> xyzLocal;

        // ubvec: array of size ncon to specify imbalance for every vertex weigth.
        // 1 is perfect balance and nparts perfect imbalance. Here 1 for now
        std::vector<ValueType> ubvec;

        //local number of edges; number of node weights; flag about edge and vertex weights 
        IndexType numWeights=0, wgtFlag=0;

        // options: array of integers for passing arguments.
        std::vector<IndexType> options;

        IndexType newLocalN = aux<IndexType,ValueType>::toMetisInterface(
            graph, coords, nodeWeights, settings, vtxDist, xadj, adjncy,
            vVwgt, tpwgts, wgtFlag, numWeights, ubvec, xyzLocal, options );

        if( newLocalN==-1){
            return scai::lama::DenseVector<IndexType>(0,0);
        }

        // nparts: the number of parts to partition (=k)
        IndexType nparts= settings.numBlocks;
        // ndims: the number of dimensions
        IndexType ndims = settings.dimensions;      
        // numflag: 0 for C-style (start from 0), 1 for Fortran-style (start from 1)
        IndexType numflag= 0;          
        // edges weights not supported
        IndexType* adjwgt= NULL;

        //parmetis requires weights to be integers
        std::vector<IndexType> vwgt( vVwgt.begin(), vVwgt.end() );


        //
        // OUTPUT parameters
        //

        // edgecut: the size of cut
        IndexType edgecut;

        // comm: the MPI comunicator
        MPI_Comm metisComm;
        MPI_Comm_dup(MPI_COMM_WORLD, &metisComm);
        //int metisRet;

        //PRINT(*comm<< ": xadj.size()= "<< sizeof(xadj) << "  adjncy.size=" <<sizeof(adjncy) );
        //PRINT(*comm << ": "<< sizeof(xyzLocal)/sizeof(real_t) << " ## "<< sizeof(partKway)/sizeof(idx_t) << " , localN= "<< localN);

        //if(comm->getRank()==0){
        //	PRINT("dims=" << ndims << ", nparts= " << nparts<<", ubvec= "<< ubvec << ", options="<< *options << ", wgtflag= "<< wgtflag );
        //}

        //
        // get the partitions with parMetis
        //

        std::chrono::time_point<std::chrono::system_clock> beforePartTime =  std::chrono::system_clock::now();

        if( parMetisGeom==0) {
            /*metisRet = */ParMETIS_V3_PartKway( 
                vtxDist.data(), xadj.data(), adjncy.data(), vwgt.data(), adjwgt, &wgtFlag, &numflag, &numWeights, &nparts, tpwgts.data(), ubvec.data(), options.data(), &edgecut, partKway, &metisComm );
        } else if( parMetisGeom==1 ) {
            ParMETIS_V3_PartGeomKway( vtxDist.data(), xadj.data(), adjncy.data(), vwgt.data(), adjwgt, &wgtFlag, &numflag, &ndims, xyzLocal.data(), &numWeights, &nparts, tpwgts.data(), ubvec.data(), options.data(), &edgecut, partKway, &metisComm );
        } else if( parMetisGeom==2 ) {
            ParMETIS_V3_PartGeom( vtxDist.data(), &ndims, xyzLocal.data(), partKway, &metisComm );
        } else {
            //repartition

            //TODO: check if vsize is correct
            idx_t* vsize = new idx_t[localN];
            for(unsigned int i=0; i<localN; i++) {
                vsize[i] = 1;
            }

            /*
            //TODO-CHECK: does repartition requires edge weights?
            IndexType localM = graph.getLocalNumValues();
            adjwgt =  new idx_t[localM];
            for(unsigned int i=0; i<localM; i++){
            	adjwgt[i] = 1;
            }
            */
            real_t itr = 1000;	//TODO: check other values too

            ParMETIS_V3_AdaptiveRepart( vtxDist.data(), xadj.data(), adjncy.data(), vwgt.data(), vsize, adjwgt, &wgtFlag, &numflag, &numWeights, &nparts, tpwgts.data(), ubvec.data(), &itr, options.data(), &edgecut, partKway, &metisComm );

            delete[] vsize;
        }
        PRINT0("\n\t\tedge cut returned by parMetis: " << edgecut <<"\n");

        std::chrono::duration<double> partitionKwayTime = std::chrono::system_clock::now() - beforePartTime;
        double partKwayTime= comm->max(partitionKwayTime.count() );
        sumKwayTime += partKwayTime;

        if( comm->getRank()==0 ) {
            std::cout<< "Running time for run number " << r << " is " << partKwayTime << std::endl;
        }
      
        if( sumKwayTime>HARD_TIME_LIMIT) {
            std::cout<< "Stopping runs because of excessive running total running time: " << sumKwayTime << std::endl;
            break;
        }
    }

    if( r!=repeatTimes) {		// in case it has to break before all the runs are completed
        repeatTimes = r+1;
    }
    if(comm->getRank()==0 ) {
        std::cout<<"Number of runs: " << repeatTimes << std::endl;
    }

    metrics.MM["timeFinalPartition"] = sumKwayTime/(ValueType)repeatTimes;


    //
    // convert partition to a DenseVector
    //
    scai::lama::DenseVector<IndexType> partitionKway(dist, IndexType(0));
    for(unsigned int i=0; i<localN; i++) {
        partitionKway.getLocalValues()[i] = partKway[i];
    }

    // check correct transformation to DenseVector
    for(int i=0; i<localN; i++) {
        //PRINT(*comm << ": "<< part[i] << " _ "<< partition.getLocalValues()[i] );
        assert( partKway[i]== partitionKway.getLocalValues()[i]);
    }

    delete[] partKway;

    return partitionKway;

}
//-----------------------------------------------------------------------------------------

//
//TODO: parMetis assumes that vertices are stored in a consecutive manner. This is not true for a
//		general distribution. Must reindex vertices for parMetis
//
template<typename IndexType, typename ValueType>
scai::lama::DenseVector<IndexType> Wrappers<IndexType, ValueType>::metisRepartition (
    const scai::lama::CSRSparseMatrix<ValueType> &graph,
    const std::vector<scai::lama::DenseVector<ValueType>> &coords,
    const std::vector<scai::lama::DenseVector<ValueType>> &nodeWeights,
    bool nodeWeightsFlag,
    struct Settings &settings,
    Metrics<ValueType> &metrics) {

    // copy graph and reindex
    scai::lama::CSRSparseMatrix<ValueType> copyGraph = graph;
    GraphUtils<IndexType, ValueType>::reindex(copyGraph);

    /*
    {// check that inidces are consecutive, TODO: maybe not needed, remove?

    	const scai::dmemo::DistributionPtr dist( copyGraph.getRowDistributionPtr() );
    	//scai::hmemo::HArray<IndexType> myGlobalIndexes;
    	//dist.getOwnedIndexes( myGlobalIndexes );
    	const IndexType globalN = graph.getNumRows();
    	const IndexType localN= dist->getLocalSize();
    	const scai::dmemo::CommunicatorPtr comm = dist->getCommunicatorPtr();

    	std::vector<IndexType> myGlobalIndexes(localN);
    	for(IndexType i=0; i<localN; i++){
    		myGlobalIndexes[i] = dist->local2global( i );
    	}

    	std::sort( myGlobalIndexes.begin(), myGlobalIndexes.end() );
    	SCAI_ASSERT_GE_ERROR( myGlobalIndexes[0], 0, "Invalid index");
    	SCAI_ASSERT_LE_ERROR( myGlobalIndexes.back(), globalN, "Invalid index");

    	for(IndexType i=1; i<localN; i++){
    		SCAI_ASSERT_EQ_ERROR( myGlobalIndexes[i], myGlobalIndexes[i-1]+1, *comm << ": Invalid index for local index " << i);
    	}

    	//PRINT(*comm << ": min global ind= " <<  myGlobalIndexes.front() << " , max global ind= " << myGlobalIndexes.back() );
    }
    */

    //trying Moritz version that also redistributes coordinates
    const scai::dmemo::DistributionPtr dist( copyGraph.getRowDistributionPtr() );
    //SCAI_ASSERT_NE_ERROR(dist->getBlockDistributionSize(), nIndex, "Reindexed distribution should be a block distribution.");
    SCAI_ASSERT_EQ_ERROR(graph.getNumRows(), copyGraph.getNumRows(), "Graph sizes must be equal.");

    std::vector<scai::lama::DenseVector<ValueType>> copyCoords = coords;
    std::vector<scai::lama::DenseVector<ValueType>> copyNodeWeights = nodeWeights;

    // TODO: use constuctor to redistribute or a Redistributor
    for (IndexType d = 0; d < settings.dimensions; d++) {
        copyCoords[d].redistribute(dist);
    }

    if (nodeWeights.size() > 0) {
        for( unsigned int i=0; i<nodeWeights.size(); i++ ) {
            copyNodeWeights[i].redistribute(dist);
        }
    }

    int parMetisVersion = 3; // flag for repartition
    scai::lama::DenseVector<IndexType> partition = Wrappers<IndexType, ValueType>::metisPartition( copyGraph, copyCoords, copyNodeWeights, nodeWeightsFlag, parMetisVersion, settings, metrics);

    //because of the reindexing, we must redistribute the partition
    partition.redistribute( graph.getRowDistributionPtr() );

    return partition;
    //return Wrappers<IndexType, ValueType>::metisPartition( copyGraph, coords, nodeWeights, nodeWeightsFlag, parMetisVersion, settings, metrics);
}


//---------------------------------------------------------
//						zoltan
//---------------------------------------------------------

template<typename IndexType, typename ValueType>
scai::lama::DenseVector<IndexType> Wrappers<IndexType, ValueType>::zoltanPartition (
    const scai::lama::CSRSparseMatrix<ValueType> &graph,
    const std::vector<scai::lama::DenseVector<ValueType>> &coords,
    const std::vector<scai::lama::DenseVector<ValueType>> &nodeWeights,
    bool nodeWeightsFlag,
    std::string algo,
    struct Settings &settings,
    Metrics<ValueType> &metrics) {

    const scai::dmemo::CommunicatorPtr comm = scai::dmemo::Communicator::getCommunicatorPtr();
    PRINT0("\t\tStarting the zoltan wrapper for partition with "<< algo);

    bool repart = false;

    return Wrappers<IndexType, ValueType>::zoltanCore( coords, nodeWeights, nodeWeightsFlag, algo, repart, settings, metrics);
}
//---------------------------------------------------------------------------------------

template<typename IndexType, typename ValueType>
scai::lama::DenseVector<IndexType> Wrappers<IndexType, ValueType>::zoltanRepartition (
    const scai::lama::CSRSparseMatrix<ValueType> &graph,
    const std::vector<scai::lama::DenseVector<ValueType>> &coords,
    const std::vector<scai::lama::DenseVector<ValueType>> &nodeWeights,
    bool nodeWeightsFlag,
    std::string algo,
    struct Settings &settings,
    Metrics<ValueType> &metrics) {

    const scai::dmemo::CommunicatorPtr comm = scai::dmemo::Communicator::getCommunicatorPtr();
    PRINT0("\t\tStarting the zoltan wrapper for repartition with " << algo);

    bool repart = true;

    return Wrappers<IndexType, ValueType>::zoltanCore( coords, nodeWeights, nodeWeightsFlag, algo, repart, settings, metrics);
}
//---------------------------------------------------------------------------------------

//relevant code can be found in zoltan, in Trilinos/packages/zoltan2/test/partition

template<typename IndexType, typename ValueType>
scai::lama::DenseVector<IndexType> Wrappers<IndexType, ValueType>::zoltanCore (
    const std::vector<scai::lama::DenseVector<ValueType>> &coords,
    const std::vector<scai::lama::DenseVector<ValueType>> &nodeWeights,
    bool nodeWeightsFlag,
    std::string algo,
    bool repart,
    struct Settings &settings,
    Metrics<ValueType> &metrics) {


/*
    typedef Zoltan2::BasicUserTypes<ValueType, IndexType, IndexType> myTypes;
    typedef Zoltan2::BasicVectorAdapter<myTypes> inputAdapter_t;

    const scai::dmemo::CommunicatorPtr comm = scai::dmemo::Communicator::getCommunicatorPtr();
    const scai::dmemo::DistributionPtr dist = coords[0].getDistributionPtr();
    const IndexType thisPE = comm->getRank();
    const IndexType numBlocks = settings.numBlocks;

    IndexType dimensions = settings.dimensions;
    IndexType localN= dist->getLocalSize();

    //TODO: point directly to the localCoords data and save time and space for zoltanCoords
    // the local part of coordinates for zoltan
    ValueType *zoltanCoords = new ValueType [dimensions * localN];

    std::vector<scai::hmemo::HArray<ValueType>> localPartOfCoords( dimensions );
    for(unsigned int d=0; d<dimensions; d++) {
        localPartOfCoords[d] = coords[d].getLocalValues();
    }
    IndexType coordInd = 0;
    for(int d=0; d<dimensions; d++) {
        //SCAI_ASSERT_LE_ERROR( dimensions*(i+1), dimensions*localN, "Too large index, localN= " << localN );
        for(IndexType i=0; i<localN; i++) {
            SCAI_ASSERT_LT_ERROR( coordInd, localN*dimensions, "Too large coordinate index");
            zoltanCoords[coordInd++] = localPartOfCoords[d][i];
        }
    }

    std::vector<const ValueType *>coordVec( dimensions );
    std::vector<int> coordStrides(dimensions);

    coordVec[0] = zoltanCoords; 	// coordVec[d] = localCoords[d].data(); or something
    coordStrides[0] = 1;

    for( int d=1; d<dimensions; d++) {
        coordVec[d] = coordVec[d-1] + localN;
        coordStrides[d] = 1;
    }

    ///////////////////////////////////////////////////////////////////////
    // Create parameters

    ValueType tolerance = 1+settings.epsilon;

    if (thisPE == 0)
        std::cout << "Imbalance tolerance is " << tolerance << std::endl;

    Teuchos::ParameterList params("test params");
    //params.set("debug_level", "basic_status");
    params.set("debug_level", "no_status");
    params.set("debug_procs", "0");
    params.set("error_check_level", "debug_mode_assertions");

    params.set("algorithm", algo);
    params.set("imbalance_tolerance", tolerance );
    params.set("num_global_parts", (int)numBlocks );

    params.set("compute_metrics", false);

    // chose if partition or repartition
    if( repart ) {
        params.set("partitioning_approach", "repartition");
    } else {
        params.set("partitioning_approach", "partition");
    }

    //TODO:	params.set("partitioning_objective", "minimize_cut_edge_count");
    //		or something else, check at
    //		https://trilinos.org/docs/r12.12/packages/zoltan2/doc/html/z2_parameters.html

    // Create global ids for the coordinates.
    IndexType *globalIds = new IndexType [localN];
    IndexType offset = thisPE * localN;

    //TODO: can also be taken from the distribution?
    for (size_t i=0; i < localN; i++)
        globalIds[i] = offset++;

    //set node weights
    //see also: Trilinos/packages/zoltan2/test/partition/MultiJaggedTest.cpp, ~line 590
    const IndexType numWeights = nodeWeights.size();
    std::vector<std::vector<ValueType>> localWeights( numWeights, std::vector<ValueType>( localN, 1.0) );
    //localWeights[i][j] is the j-th weight of the i-th vertex (i is local ID)

    if( nodeWeightsFlag ) {
        for( unsigned int w=0; w<numWeights; w++ ) {
            scai::hmemo::ReadAccess<ValueType> rLocalWeights( nodeWeights[w].getLocalValues() );
            for(unsigned int i=0; i<localN; i++) {
                localWeights[w][i] = rLocalWeights[i];
            }
        }
    } else {
        //all weights are initiallized with unit weight
    }

    std::vector<const ValueType *>weightVec( numWeights );
    for( unsigned int w=0; w<numWeights; w++ ) {
        weightVec[w] = localWeights[w].data();
    }

    //if it is stride.size()==0, it assumed that all strides are 1
    std::vector<int> weightStrides; //( numWeights, 1);

    //create the problem and solve it
    inputAdapter_t *ia= new inputAdapter_t(localN, globalIds, coordVec,
                                           coordStrides, weightVec, weightStrides);

    Zoltan2::PartitioningProblem<inputAdapter_t> *problem =
        new Zoltan2::PartitioningProblem<inputAdapter_t>(ia, &params);

    if( comm->getRank()==0 )
        std::cout<< "About to call zoltan, algo " << algo << std::endl;

    int repeatTimes = settings.repeatTimes;
    double sumPartTime = 0.0;
    int r=0;

    for( r=0; r<repeatTimes; r++) {
        std::chrono::time_point<std::chrono::system_clock> beforePartTime =  std::chrono::system_clock::now();
        problem->solve();

        std::chrono::duration<double> partitionTmpTime = std::chrono::system_clock::now() - beforePartTime;
        double partitionTime= comm->max(partitionTmpTime.count() );
        sumPartTime += partitionTime;
        if( comm->getRank()==0 ) {
            std::cout<< "Running time for run number " << r << " is " << partitionTime << std::endl;
        }
        if( sumPartTime>HARD_TIME_LIMIT) {
            std::cout<< "Stopping runs because of excessive running total running time: " << sumPartTime << std::endl;
            break;
        }
    }

    if( r!=repeatTimes) {		// in case it has to break before all the runs are completed
        repeatTimes = r+1;
    }
    if(comm->getRank()==0 ) {
        std::cout<<"Number of runs: " << repeatTimes << std::endl;
    }

    metrics.MM["timeFinalPartition"] = sumPartTime/(ValueType)repeatTimes;

    //
    // convert partition to a DenseVector
    //
    scai::lama::DenseVector<IndexType> partitionZoltan(dist, IndexType(0));

    //std::vector<IndexType> localBlockSize( numBlocks, 0 );

    const Zoltan2::PartitioningSolution<inputAdapter_t> &solution = problem->getSolution();
    const int *partAssignments = solution.getPartListView();
    for(unsigned int i=0; i<localN; i++) {
        IndexType thisBlock = partAssignments[i];
        SCAI_ASSERT_LT_ERROR( thisBlock, numBlocks, "found wrong vertex id");
        SCAI_ASSERT_GE_ERROR( thisBlock, 0, "found negetive vertex id");
        partitionZoltan.getLocalValues()[i] = thisBlock;
        //localBlockSize[thisBlock]++;
    }
    for(int i=0; i<localN; i++) {
        //PRINT(*comm << ": "<< part[i] << " _ "<< partition.getLocalValues()[i] );
        SCAI_ASSERT_EQ_ERROR( partitionZoltan.getLocalValues()[i], partAssignments[i], "Wrong conversion to DenseVector");
    }

    delete[] globalIds;
    delete[] zoltanCoords;

    return partitionZoltan;
    */

return scai::lama::DenseVector<IndexType> (coords[0].getDistributionPtr(), IndexType(0));
}

//---------------------------------------------------------------------------------------

//template class Wrappers<IndexType, double>;
//template class Wrappers<IndexType, float>;
template class Wrappers<IndexType, real_t>;

}//namespace
