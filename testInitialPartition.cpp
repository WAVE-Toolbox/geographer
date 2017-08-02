#include <scai/lama.hpp>

#include <scai/lama/matrix/all.hpp>
#include <scai/lama/matutils/MatrixCreator.hpp>

#include <scai/dmemo/BlockDistribution.hpp>
#include <scai/dmemo/Distribution.hpp>

#include <scai/hmemo/Context.hpp>
#include <scai/hmemo/HArray.hpp>

#include <scai/utilskernel/LArray.hpp>
#include <scai/lama/Vector.hpp>

#include <boost/algorithm/string.hpp>
#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>

#include <memory>
#include <cstdlib>
#include <chrono>

#include "MeshGenerator.h"
#include "FileIO.h"
#include "ParcoRepart.h"
#include "Settings.h"
#include "MultiLevel.h"
#include "LocalRefinement.h"
#include "SpectralPartition.h"
#include "MultiSection.h"
#include "KMeans.h"

typedef double ValueType;
typedef int IndexType;


/**
 *  Examples of use:
 * 
 *  for reading from file "fileName" 
 * ./a.out --graphFile fileName --epsilon 0.05 --minBorderNodes=10 --dimensions=2 --borderDepth=10  --stopAfterNoGainRounds=3 --minGainForNextGlobalRound=10
 * 
 * for generating a 10x20x30 mesh
 * ./a.out --generate --numX=10 --numY=20 --numZ=30 --epsilon 0.05 --sfcRecursionSteps=10 --dimensions=3 --borderDepth=10  --stopAfterNoGainRounds=3 --minGainForNextGlobalRound=10
 * 
 * !! for now, when reading a file --dimensions must be 2
 */

//----------------------------------------------------------------------------

namespace ITI {
	std::istream& operator>>(std::istream& in, Format& format)
	{
		std::string token;
		in >> token;
		if (token == "AUTO" or token == "0")
			format = ITI::Format::AUTO ;
		else if (token == "METIS" or token == "1")
			format = ITI::Format::METIS;
		else if (token == "ADCIRC" or token == "2")
			format = ITI::Format::ADCIRC;
		else if (token == "OCEAN" or token == "3")
			format = ITI::Format::OCEAN;
		else
			in.setstate(std::ios_base::failbit);
		return in;
	}

	std::ostream& operator<<(std::ostream& out, Format& method)
	{
		std::string token;

		if (method == ITI::Format::AUTO)
			token = "AUTO";
		else if (method == ITI::Format::METIS)
			token = "METIS";
		else if (method == ITI::Format::ADCIRC)
			token = "ADCIRC";
		else if (method == ITI::Format::OCEAN)
			token = "OCEAN";
		out << token;
		return out;
	}
}


int main(int argc, char** argv) {
	using namespace boost::program_options;
        options_description desc("Supported options");
        
        struct Settings settings;
        //ITI::Format ff = ITI::Format::METIS;
        IndexType ff = 1;
        
        desc.add_options()
            ("help", "display options")
            ("version", "show version")
            ("graphFile", value<std::string>(), "read graph from file")
            ("coordFile", value<std::string>(), "coordinate file. If none given, assume that coordinates for graph arg are in file arg.xyz")
            ("fileFormat", value<int>(&ff)->default_value(ff), "The format of the file to read: 0 is for AUTO format, 1 for METIS, 2 for ADCRIC, 3 for OCEAN, 4 for MatrixMarket format. See FileIO.h for more details.")
            ("generate", "generate random graph. Currently, only uniform meshes are supported.")
            ("dimensions", value<int>(&settings.dimensions)->default_value(settings.dimensions), "Number of dimensions of generated graph")
            ("numX", value<int>(&settings.numX)->default_value(settings.numX), "Number of points in x dimension of generated graph")
            ("numY", value<int>(&settings.numY)->default_value(settings.numY), "Number of points in y dimension of generated graph")
            ("numZ", value<int>(&settings.numZ)->default_value(settings.numZ), "Number of points in z dimension of generated graph")
            ("epsilon", value<double>(&settings.epsilon)->default_value(settings.epsilon), "Maximum imbalance. Each block has at most 1+epsilon as many nodes as the average.")
            ("numBlocks", value<IndexType>(&settings.numBlocks), "Number of blocks to partition to")
            ("minBorderNodes", value<int>(&settings.minBorderNodes)->default_value(settings.minBorderNodes), "Tuning parameter: Minimum number of border nodes used in each refinement step")
            ("stopAfterNoGainRounds", value<int>(&settings.stopAfterNoGainRounds)->default_value(settings.stopAfterNoGainRounds), "Tuning parameter: Number of rounds without gain after which to abort localFM. A value of 0 means no stopping.")
            ("initialPartition",  value<int> (&settings.initialPartition), "Parameter for different initial partition: 0 for the hilbert space filling curve, 1 for the pixeled method, 2 for spectral parition")
            ("bisect", value<bool>(&settings.bisect)->default_value(settings.bisect), "Used for the multisection method. If set to true the algorithm perfoms bisections (not multisection) until the desired number of parts is reached")
            ("cutsPerDim", value<std::vector<IndexType>>(&settings.cutsPerDim)->multitoken(), "If msOption=2, then provide d values that define the number of cuts per dimension.")
            ("pixeledSideLen", value<int>(&settings.pixeledSideLen)->default_value(settings.pixeledSideLen), "The resolution for the pixeled partition or the spectral")
            ("minGainForNextGlobalRound", value<int>(&settings.minGainForNextRound)->default_value(settings.minGainForNextRound), "Tuning parameter: Minimum Gain above which the next global FM round is started")
            ("gainOverBalance", value<bool>(&settings.gainOverBalance)->default_value(settings.gainOverBalance), "Tuning parameter: In local FM step, choose queue with best gain over queue with best balance")
            ("useDiffusionTieBreaking", value<bool>(&settings.useDiffusionTieBreaking)->default_value(settings.useDiffusionTieBreaking), "Tuning Parameter: Use diffusion to break ties in Fiduccia-Mattheyes algorithm")
            ("useGeometricTieBreaking", value<bool>(&settings.useGeometricTieBreaking)->default_value(settings.useGeometricTieBreaking), "Tuning Parameter: Use distances to block center for tie breaking")
            ("skipNoGainColors", value<bool>(&settings.skipNoGainColors)->default_value(settings.skipNoGainColors), "Tuning Parameter: Skip Colors that didn't result in a gain in the last global round")
            ("multiLevelRounds", value<int>(&settings.multiLevelRounds)->default_value(settings.multiLevelRounds), "Tuning Parameter: How many multi-level rounds with coarsening to perform")
            ;
        
        variables_map vm;
        store(command_line_parser(argc, argv).options(desc).run(), vm);
        notify(vm);
        
        if (vm.count("help")) {
            std::cout << desc << "\n";
            return 0;
        }
        
        if (vm.count("version")) {
            std::cout << "Git commit " << version << std::endl;
            return 0;
        }
        
        if (vm.count("generate") && vm.count("file")) {
            std::cout << "Pick one of --file or --generate" << std::endl;
            return 0;
        }
        
        if (vm.count("generate") && (vm["dimensions"].as<int>() != 3)) {
            std::cout << "Mesh generation currently only supported for three dimensions" << std::endl;
            return 0;
        }
        
        //if( vm.count("cutsPerDim")){
            
        IndexType N = -1; 		// total number of points
        
        char machineChar[255];
        std::string machine;
        gethostname(machineChar, 255);
        if (machineChar) {
            machine = std::string(machineChar);
        } else {
            std::cout << "machine char not valid" << std::endl;
        }
        
        scai::lama::CSRSparseMatrix<ValueType> graph; 	// the adjacency matrix of the graph
        std::vector<DenseVector<ValueType>> coordinates(settings.dimensions); // the coordinates of the graph
        scai::lama::DenseVector<ValueType> nodeWeights;     // node weights
        
        std::vector<ValueType> maxCoord(settings.dimensions); // the max coordinate in every dimensions, used only for 3D
        
        scai::dmemo::CommunicatorPtr comm = scai::dmemo::Communicator::getCommunicatorPtr();
        
        IndexType initialPartition = settings.initialPartition;
        
        /* timing information
         */
        std::chrono::time_point<std::chrono::system_clock> startTime = std::chrono::system_clock::now();
        
        if (comm->getRank() == 0){
            std::string inputstring;
            if (vm.count("graphFile")) {
    		inputstring = vm["graphFile"].as<std::string>();
    	} else if (vm.count("quadTreeFile")) {
    		inputstring = vm["quadTreeFile"].as<std::string>();
    	} else {
    		inputstring = "generate";
    	}
        std::cout<< "commit:"<< version<<  " input:"<< inputstring << std::endl;
	}

    std::string graphFile;
	
    if (vm.count("graphFile")) {
        if (comm->getRank() == 0){
            std::cout<< "input: graphFile" << std::endl;
        }
    	graphFile = vm["graphFile"].as<std::string>();
    	std::string coordFile;
    	if (vm.count("coordFile")) {
    		coordFile = vm["coordFile"].as<std::string>();
    	} else {
    		coordFile = graphFile + ".xyz";
    	}
    
        std::fstream f(graphFile);

        if(f.fail()){
            throw std::runtime_error("File "+ graphFile + " failed.");
        }
        
        if (comm->getRank() == 0)
        {
            std::cout<< "Reading from file \""<< graphFile << "\" for the graph and \"" << coordFile << "\" for coordinates"<< std::endl;
            std::cout<< "File format: " << ff << std::endl;
        }

        // read the adjacency matrix and the coordinates from a file  
        
        ITI::Format format = static_cast<ITI::Format>(ff);
        
        if (vm.count("fileFormat")) {
            graph = ITI::FileIO<IndexType, ValueType>::readGraph( graphFile, format );
        } else{
            graph = ITI::FileIO<IndexType, ValueType>::readGraph( graphFile );
        }
        N = graph.getNumRows();
        scai::dmemo::DistributionPtr rowDistPtr = graph.getRowDistributionPtr();
        scai::dmemo::DistributionPtr noDistPtr( new scai::dmemo::NoDistribution( N ));
        assert(graph.getColDistribution().isEqual(*noDistPtr));
        
        // for 2D we do not know the size of every dimension
        settings.numX = N;
        settings.numY = 1;
        settings.numZ = 1;
        
        
        if (vm.count("fileFormat")) {
            coordinates = ITI::FileIO<IndexType, ValueType>::readCoords(coordFile, N, settings.dimensions, format);
        } else {
            coordinates = ITI::FileIO<IndexType, ValueType>::readCoords(coordFile, N, settings.dimensions);
        }
        PRINT0("read  graph and coordinates");        
        
        //unit weights
        scai::hmemo::HArray<ValueType> localWeights( rowDistPtr->getLocalSize(), 1 );
        nodeWeights.swap( localWeights, rowDistPtr );

        if (comm->getRank() == 0) {
            std::cout << "Read " << N << " points." << std::endl;
            std::cout << "Read coordinates." << std::endl;
            std::cout << "On average there are about " << N/comm->getSize() << " points per PE."<<std::endl;
        }

    }
    else if(vm.count("generate")){
        if (comm->getRank() == 0){
            std::cout<< "input: generate" << std::endl;
        }
        if (settings.dimensions == 2) {
            settings.numZ = 1;
        }
        
        N = settings.numX * settings.numY * settings.numZ;
        
        maxCoord[0] = settings.numX;
        maxCoord[1] = settings.numY;
        maxCoord[2] = settings.numZ;
        
        std::vector<IndexType> numPoints(3); // number of points in each dimension, used only for 3D
        
        for (IndexType i = 0; i < 3; i++) {
            numPoints[i] = maxCoord[i];
        }
        
        if( comm->getRank()== 0){
            std::cout<< "Generating for dim= "<< settings.dimensions << " and numPoints= "<< settings.numX << ", " << settings.numY << ", "<< settings.numZ << ", in total "<< N << " number of points" << std::endl;
            std::cout<< "\t\t and maxCoord= "<< maxCoord[0] << ", "<< maxCoord[1] << ", " << maxCoord[2]<< std::endl;
        }
        
        scai::dmemo::DistributionPtr rowDistPtr ( scai::dmemo::Distribution::getDistributionPtr( "BLOCK", comm, N) );
        scai::dmemo::DistributionPtr noDistPtr(new scai::dmemo::NoDistribution(N));
        graph = scai::lama::CSRSparseMatrix<ValueType>( rowDistPtr , noDistPtr );
        
        scai::dmemo::DistributionPtr coordDist ( scai::dmemo::Distribution::getDistributionPtr( "BLOCK", comm, N) );
        for(IndexType i=0; i<settings.dimensions; i++){
            coordinates[i].allocate(coordDist);
            coordinates[i] = static_cast<ValueType>( 0 );
        }

        // create the adjacency matrix and the coordinates
        ITI::MeshGenerator<IndexType, ValueType>::createStructured3DMesh_dist( graph, coordinates, maxCoord, numPoints);
        
        IndexType nodes= graph.getNumRows();
        IndexType edges= graph.getNumValues()/2;	
        
        if(comm->getRank()==0){
            std::cout<< "Generated random 3D graph with "<< nodes<< " and "<< edges << " edges."<< std::endl;
        }
        
    }
    else{
    	std::cout << "Only input file as input. Call again with --graphFile" << std::endl;
    	return 0;
    }
    
    // time needed to get the input
    std::chrono::duration<double> inputTime = std::chrono::system_clock::now() - startTime;

    assert(N > 0);

    if (comm->getSize() > 0) {
    	//settings.numBlocks = comm->getSize();
    }
    
    if( !vm.count("numBlocks") ){
        settings.numBlocks = comm->getSize();
    }
    
    //----------
    
    scai::dmemo::DistributionPtr rowDistPtr = graph.getRowDistributionPtr();
    scai::dmemo::DistributionPtr noDistPtr( new scai::dmemo::NoDistribution( N ));
    
    DenseVector<IndexType> uniformWeights;
    
    settings.minGainForNextRound = 10;
    settings.minBorderNodes = 10;
    settings.useGeometricTieBreaking = 1;
    settings.pixeledSideLen = int ( std::min(settings.numBlocks, 100) );
    settings.fileFormat = ff;
    
    std::string destPath = "./partResults/testInitial/blocks_"+std::to_string(settings.numBlocks)+"/";
    boost::filesystem::create_directories( destPath );   
    
    std::size_t found= graphFile.std::string::find_last_of("/");
    std::string logFile = destPath + "results_" + graphFile.substr(found+1)+ ".log";
    std::ofstream logF(logFile);
    std::ifstream f(graphFile);
    
    if( comm->getRank()==0){ 
        settings.print( std::cout );
        std::cout<< std::endl;
    }

    IndexType dimensions = settings.dimensions;
    IndexType k = settings.numBlocks;
    
    std::chrono::time_point<std::chrono::system_clock> beforeInitialTime;
    std::chrono::duration<double> partitionTime;
    std::chrono::duration<double> finalPartitionTime;
    
    
    using namespace ITI;
    
    //TODO: not needed to redistribute. Reading graph and coordinates already distributes the data.
    /*
    graph.redistribute( rowDistPtr, noDistPtr);
    for(int d=0; d<dimensions; d++){
        coordinates[d].redistribute( rowDistPtr );
    } 
    */
    
    if(comm->getRank()==0) std::cout <<std::endl<<std::endl;
    
    scai::lama::DenseVector<IndexType> partition;
    
    comm->synchronize();
    
    switch( initialPartition ){
        case 0:{  //------------------------------------------- hilbert/sfc
           
            beforeInitialTime =  std::chrono::system_clock::now();
            PRINT0( "Get a hilbert/sfc partition");
            
            // get a hilbertPartition
            partition = ParcoRepart<IndexType, ValueType>::hilbertPartition( graph, coordinates, settings);
            
            partitionTime =  std::chrono::system_clock::now() - beforeInitialTime;
            
            assert( partition.size() == N);
            assert( coordinates[0].size() == N);
            
            break;
        }
        case 1:{  //------------------------------------------- pixeled
  
            beforeInitialTime =  std::chrono::system_clock::now();
            PRINT0( "Get a pixeled partition");
            
            // get a pixelPartition
            partition = ParcoRepart<IndexType, ValueType>::pixelPartition( graph, coordinates, settings);
            
            partitionTime =  std::chrono::system_clock::now() - beforeInitialTime;
            
            assert( partition.size() == N);
            assert( coordinates[0].size() == N);
            
            break;
        }
        case 2:{  //------------------------------------------- spectral
            std::cout<< "Not included in testInitial yet, choose another option."<< std::endl;
            std::terminate();
        }
        case 3:{  //------------------------------------------- k-means
            beforeInitialTime =  std::chrono::system_clock::now();
            PRINT0( "Get a k-means partition");
            
            // get a k-means partition
            //partition = ParcoRepart<IndexType, ValueType>::partitionGraph( graph, coordinates, settings);
            
            DenseVector<IndexType> tempResult = ParcoRepart<IndexType, ValueType>::hilbertPartition(graph, coordinates, settings);
            
            scai::hmemo::HArray<IndexType> localWeightsInt( rowDistPtr->getLocalSize(), 1 );
            scai::lama::DenseVector<IndexType> nodeWeightsInt;     // node weights
            nodeWeightsInt.swap( localWeightsInt, rowDistPtr );
            nodeWeightsInt.redistribute(tempResult.getDistributionPtr());
            
            const IndexType weightSum = nodeWeightsInt.sum().Scalar::getValue<IndexType>();
            const std::vector<IndexType> blockSizes(settings.numBlocks, weightSum/settings.numBlocks);
            
            partition = ITI::KMeans::computePartition(coordinates, settings.numBlocks, nodeWeightsInt, blockSizes, settings.epsilon);      
            
            partitionTime =  std::chrono::system_clock::now() - beforeInitialTime;
            
            assert( partition.size() == N);
            assert( coordinates[0].size() == N);
            break;
        }
        case 4:{  //------------------------------------------- multisection
            
            if ( settings.bisect==1){
                PRINT0( "Get a partition with bisection");
            }else{
                PRINT0( "Get a partition with multisection");
            }
            
            beforeInitialTime =  std::chrono::system_clock::now();
            
            // get a multisection partition
            partition =  MultiSection<IndexType, ValueType>::getPartitionNonUniform( graph, coordinates, nodeWeights, settings);
            
            partitionTime =  std::chrono::system_clock::now() - beforeInitialTime;
            
            assert( partition.size() == N);
            assert( coordinates[0].size() == N);
            
             //get new distribution
            scai::dmemo::DistributionPtr newDist( new scai::dmemo::GeneralDistribution ( *rowDistPtr, partition.getLocalValues() ) );
    
            partition.redistribute( newDist);
            
            graph.redistribute(newDist, graph.getColDistributionPtr());
    
            // redistibute coordinates
            for (IndexType dim = 0; dim < dimensions; dim++) {
                coordinates[dim].redistribute( newDist );
            }
            // check coordinates size
            for (IndexType dim = 0; dim < dimensions; dim++) {
                assert( coordinates[dim].size() == N);
                assert( coordinates[dim].getLocalValues().size() == newDist->getLocalSize() );
            }
            
            
            if(dimensions==2){
                ITI::FileIO<IndexType, ValueType>::writeCoordsDistributed_2D( coordinates, N, destPath+"multisectPart");
            }
            
            break;   
        }
        default:{
            PRINT0("Value "<< initialPartition << " for option initialPartition not supported" );
            break;
        }
    }
    
    
    ValueType cut = GraphUtils::computeCut<IndexType, ValueType>( graph, partition);
    ValueType imbalance = GraphUtils::computeImbalance<IndexType, ValueType>( partition, k);
    IndexType maxComm = GraphUtils::computeMaxComm<IndexType, ValueType>( graph, partition, k);
    if(comm->getRank()==0){
        logF << "--  Initial parition, total time: " << partitionTime.count() << std::endl;
        logF << "\tfinal cut= "<< cut << ", final imbalance= "<< imbalance << " ,maxComm= "<< maxComm;
        logF  << std::endl  << std::endl  << std::endl; 
        std::cout << "\033[1;36m--Initial partition, total time: " << partitionTime.count() << std::endl;
        std::cout << "\tfinal cut= "<< cut << ", final imbalance= "<< imbalance << " ,maxComm= "<< maxComm << "\033[0m";
        std::cout << std::endl  << std::endl  << std::endl;
        
        logF<< "Results for file " << graphFile << std::endl;
        logF<< "nodes= "<< N << std::endl<< std::endl;
        settings.print( logF );
        logF<< std::endl<< std::endl << "Only initial partition, no MultiLevel or LocalRefinement"<< std::endl << std::endl;
    }
    
    
    /*
     *  commenting out spectral, it is not correct yet
     * 
    //------------------------------------------- spectral
    
    // the partitioning may redistribute the input graph
    graph.redistribute(rowDistPtr, noDistPtr);
    for(int d=0; d<dimensions; d++){
        coordinates[d].redistribute( rowDistPtr );
    }
    if(comm->getRank()==0) std::cout <<std::endl<<std::endl;
    PRINT0("Get a spectral partition");

    beforeInitialTime =  std::chrono::system_clock::now();
    // get initial spectral partition
    scai::lama::DenseVector<IndexType> spectralPartition = SpectralPartition<IndexType, ValueType>::getPartition( graph, coordinates, settings);
      
    partitionTime =  std::chrono::system_clock::now() - beforeInitialTime;
    
    assert( spectralPartition.size() == N);
    assert( coordinates[0].size() == N);
    //aux::print2DGrid( graph, spectralPartition );
    //if(dimensions==2){
    //    ITI::FileIO<IndexType, ValueType>::writeCoordsDistributed_2D( coordinates, N, destPath+"spectralPart");
    //}
    cut = ParcoRepart<IndexType, ValueType>::computeCut( graph, spectralPartition);
    imbalance = ParcoRepart<IndexType, ValueType>::computeImbalance( spectralPartition, k);
    if(comm->getRank()==0){
        logF<< "-- Initial Spectral partition " << std::endl;
        logF<< "\tcut: " << cut << " , imbalance= "<< imbalance;
    }
    
    uniformWeights = DenseVector<IndexType>(graph.getRowDistributionPtr(), 1);
    ITI::MultiLevel<IndexType, ValueType>::multiLevelStep(graph, spectralPartition, uniformWeights, coordinates, settings);
    
    finalPartitionTime =  std::chrono::system_clock::now() - beforeInitialTime;
    
    //if(dimensions==2){
    //    ITI::FileIO<IndexType, ValueType>::writeCoordsDistributed_2D( coordinates, N, destPath+"finalWithSpectral");
    //}
    cut = ParcoRepart<IndexType, ValueType>::computeCut( graph, spectralPartition);
    imbalance = ParcoRepart<IndexType, ValueType>::computeImbalance( spectralPartition, k);
    if(comm->getRank()==0){
        logF<< "   After multilevel, total time: " << finalPartitionTime.count() << std::endl;
        logF<< "\tfinal cut= "<< cut << ", final imbalance= "<< imbalance;
        logF  << std::endl  << std::endl; 
    }
    
    
    logF.close();
    if(comm->getRank()==0){
        std::cout<< "Output files written in " << destPath << " in file "<< logFile <<std::endl;
    }
    */
    /*
    if (comm->getRank() == 0) {
        std::cout<< "commit:"<< version<< " input:"<< ( vm.count("graphFile") ? vm["graphFile"].as<std::string>() :"generate");
        std::cout<< " nodes:"<< N<< " dimensions:"<< settings.dimensions <<" k:" << settings.numBlocks;
        std::cout<< " epsilon:" << settings.epsilon << " minBorderNodes:"<< settings.minBorderNodes;
        std::cout<< " minGainForNextRound:" << settings.minGainForNextRound;
        std::cout<< " stopAfterNoGainRounds:"<< settings.stopAfterNoGainRounds << std::endl;
        
        std::cout<< "Cut is: "<< cut<< " and imbalance: "<< imbalance << std::endl;
        std::cout<<"inputTime:" << inputT << " partitionTime:" << partT <<" reportTime:"<< repT << std::endl;
    }
    */
}
