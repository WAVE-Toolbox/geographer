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
#include "Metrics.h"
#include "MultiLevel.h"
#include "LocalRefinement.h"
#include "SpectralPartition.h"
#include "MultiSection.h"
#include "KMeans.h"
#include "GraphUtils.h"

#include "RBC/Sort/SQuick.hpp"

/**
 *  Examples of use:
 * 
 *  for reading from file "fileName" 
 * ./a.out --graphFile fileName --epsilon 0.05 --minBorderNodes=10 --dimensions=2 --borderDepth=10  --stopAfterNoGainRounds=3 --minGainForNextGlobalRound=10
 * 
 * for generating a 10x20x30 mesh
 * ./a.out --generate --numX=10 --numY=20 --numZ=30 --epsilon 0.05 --sfcRecursionSteps=10 --dimensions=3 --borderDepth=10  --stopAfterNoGainRounds=3 --minGainForNextGlobalRound=10
 * 
 */

//----------------------------------------------------------------------------


std::istream& operator>>(std::istream& in, InitialPartitioningMethods& method)
{
    std::string token;
    in >> token;
    if (token == "SFC" or token == "0")
        method = InitialPartitioningMethods::SFC;
    else if (token == "Pixel" or token == "1")
        method = InitialPartitioningMethods::Pixel;
    else if (token == "Spectral" or token == "2")
    	method = InitialPartitioningMethods::Spectral;
    else if (token == "KMeans" or token == "Kmeans" or token == "K-Means" or token == "K-means" or token == "3")
        method = InitialPartitioningMethods::KMeans;
    else if (token == "Multisection" or token == "MultiSection" or token == "4")
    	method = InitialPartitioningMethods::Multisection;
    else
        in.setstate(std::ios_base::failbit);
    return in;
}

std::ostream& operator<<(std::ostream& out, InitialPartitioningMethods method)
{
    std::string token;

    if (method == InitialPartitioningMethods::SFC)
        token = "SFC";
    else if (method == InitialPartitioningMethods::Pixel)
    	token = "Pixel";
    else if (method == InitialPartitioningMethods::Spectral)
    	token = "Spectral";
    else if (method == InitialPartitioningMethods::KMeans)
        token = "KMeans";
    else if (method == InitialPartitioningMethods::Multisection)
    	token = "Multisection";
    out << token;
    return out;
}






int main(int argc, char** argv) {
	using namespace boost::program_options;
	options_description desc("Supported options");
	
	struct Settings settings;
	//ITI::Format ff = ITI::Format::METIS;
	ITI::Format coordFormat;
	std::string blockSizesFile;
	bool writePartition = false;
	
	//std::chrono::time_point<std::chrono::system_clock> startTime =  std::chrono::system_clock::now();
	
	desc.add_options()
	("help", "display options")
	("version", "show version")
	("graphFile", value<std::string>(), "read graph from file")
	("coordFile", value<std::string>(), "coordinate file. If none given, assume that coordinates for graph arg are in file arg.xyz")
	("fileFormat", value<ITI::Format>(&settings.fileFormat)->default_value(settings.fileFormat), "The format of the file to read: 0 is for AUTO format, 1 for METIS, 2 for ADCRIC, 3 for OCEAN, 4 for MatrixMarket format. See FileIO.h for more details.")
	("coordFormat",  value<ITI::Format>(&coordFormat), "format of coordinate file")
	
	("generate", "generate random graph. Currently, only uniform meshes are supported.")
	("dimensions", value<IndexType>(&settings.dimensions)->default_value(settings.dimensions), "Number of dimensions of generated graph")
	("numX", value<IndexType>(&settings.numX)->default_value(settings.numX), "Number of points in x dimension of generated graph")
	("numY", value<IndexType>(&settings.numY)->default_value(settings.numY), "Number of points in y dimension of generated graph")
	("numZ", value<IndexType>(&settings.numZ)->default_value(settings.numZ), "Number of points in z dimension of generated graph")
	("epsilon", value<double>(&settings.epsilon)->default_value(settings.epsilon), "Maximum imbalance. Each block has at most 1+epsilon as many nodes as the average.")
	
	("numBlocks", value<IndexType>(&settings.numBlocks), "Number of blocks to partition to")
	
	("minBorderNodes", value<IndexType>(&settings.minBorderNodes)->default_value(settings.minBorderNodes), "Tuning parameter: Minimum number of border nodes used in each refinement step")
	("stopAfterNoGainRounds", value<IndexType>(&settings.stopAfterNoGainRounds)->default_value(settings.stopAfterNoGainRounds), "Tuning parameter: Number of rounds without gain after which to abort localFM. A value of 0 means no stopping.")
	("initialPartition",  value<InitialPartitioningMethods> (&settings.initialPartition), "Parameter for different initial partition: 0 or 'SFC' for the hilbert space filling curve, 1 or 'Pixel' for the pixeled method, 2 or 'Spectral' for spectral parition, 3 or 'KMeans' for Kmeans and 4 or 'MultiSection' for Multisection")
	("bisect", value<bool>(&settings.bisect)->default_value(settings.bisect), "Used for the multisection method. If set to true the algorithm perfoms bisections (not multisection) until the desired number of parts is reached")
	("cutsPerDim", value<std::vector<IndexType>>(&settings.cutsPerDim)->multitoken(), "If msOption=2, then provide d values that define the number of cuts per dimension.")
	("pixeledSideLen", value<IndexType>(&settings.pixeledSideLen)->default_value(settings.pixeledSideLen), "The resolution for the pixeled partition or the spectral")
	("minGainForNextGlobalRound", value<IndexType>(&settings.minGainForNextRound)->default_value(settings.minGainForNextRound), "Tuning parameter: Minimum Gain above which the next global FM round is started")
	("gainOverBalance", value<bool>(&settings.gainOverBalance)->default_value(settings.gainOverBalance), "Tuning parameter: In local FM step, choose queue with best gain over queue with best balance")
	("useDiffusionTieBreaking", value<bool>(&settings.useDiffusionTieBreaking)->default_value(settings.useDiffusionTieBreaking), "Tuning Parameter: Use diffusion to break ties in Fiduccia-Mattheyes algorithm")
	("useGeometricTieBreaking", value<bool>(&settings.useGeometricTieBreaking)->default_value(settings.useGeometricTieBreaking), "Tuning Parameter: Use distances to block center for tie breaking")
	("skipNoGainColors", value<bool>(&settings.skipNoGainColors)->default_value(settings.skipNoGainColors), "Tuning Parameter: Skip Colors that didn't result in a gain in the last global round")
	("multiLevelRounds", value<IndexType>(&settings.multiLevelRounds)->default_value(settings.multiLevelRounds), "Tuning Parameter: How many multi-level rounds with coarsening to perform")
	
	("computeDiameter", value<bool>(&settings.computeDiameter)->default_value(true), "Compute diameter of resulting block files.")
	("blockSizesFile", value<std::string>(&blockSizesFile) , " file to read the block sizes for every block")
	("writePartition", "Writes the partition in the outFile.partition file")
	("outFile", value<std::string>(&settings.outFile), "write result partition into file")
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
	
	if( vm.count("cutsPerDim") ){
		SCAI_ASSERT( !settings.cutsPerDim.empty(), "options cutsPerDim was given but the vector is empty" );
		SCAI_ASSERT_EQ_ERROR(settings.cutsPerDim.size(), settings.dimensions, "cutsPerDime: user must specify d values for mutlisection using option --cutsPerDim. e.g.: --cutsPerDim=4,20 for a partition in 80 parts/" );
		IndexType tmpK = 1;
		for( const auto& i: settings.cutsPerDim){
			tmpK *= i;
		}
		settings.numBlocks= tmpK;
	}
	
	scai::dmemo::CommunicatorPtr comm = scai::dmemo::Communicator::getCommunicatorPtr();
	const IndexType thisPE = comm->getRank();
	
	if( thisPE ==0 ){
		std::cout <<"Starting file " << __FILE__ << std::endl;
		
		std::chrono::time_point<std::chrono::system_clock> now =  std::chrono::system_clock::now();
		std::time_t timeNow = std::chrono::system_clock::to_time_t(now);
		std::cout << "date and time: " << std::ctime(&timeNow) << std::endl;
	}
	
	IndexType N = -1; 		// total number of points
	
	writePartition = vm.count("writePartition");
	
	//settings.computeDiameter = vm.count("computeDiameter");
	
	char machineChar[255];
	std::string machine;
	gethostname(machineChar, 255);
	if (machineChar) {
		machine = std::string(machineChar);
		settings.machine = machine;
	} else {
		std::cout << "machine char not valid" << std::endl;
		machine = "machine char not valid";
	}
		
	
	
	/* timing information
	 */
	std::chrono::time_point<std::chrono::system_clock> startTime = std::chrono::system_clock::now();
	
	if ( thisPE == 0){
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
		
	scai::lama::CSRSparseMatrix<ValueType> graph; 	// the adjacency matrix of the graph
	std::vector<DenseVector<ValueType>> coordinates(settings.dimensions); // the coordinates of the graph
	scai::lama::DenseVector<ValueType> nodeWeights;     // node weights
	
    //---------------------------------------------------------
    //
    // generate or read graph and coordinates
    //
	
	if (vm.count("graphFile")) {
		if ( thisPE== 0){
            std::cout<< "input: graphFile" << std::endl;
        }
    	std::string  graphFile = vm["graphFile"].as<std::string>();
		settings.fileName = graphFile;
    	std::string coordFile;
    	if (vm.count("coordFile")) {
    		coordFile = vm["coordFile"].as<std::string>();
    	} else {
    		coordFile = graphFile + ".xyz";
    	}
    
        //std::fstream f(graphFile,std::ios::in);
		std::ifstream f(graphFile);
		
        if(f.fail()){
            throw std::runtime_error("Could not open file "+ graphFile );
        }
        
        if ( thisPE == 0)
        {
            std::cout<< "Reading from file \""<< graphFile << "\" for the graph and \"" << coordFile << "\" for coordinates"<< std::endl;
            std::cout<< "File format: " << settings.fileFormat << std::endl;
        }

        // read the adjacency matrix and the coordinates from a file  
        
        std::vector<DenseVector<ValueType> > vectorOfNodeWeights;
               
        if (vm.count("fileFormat")) {
            graph = ITI::FileIO<IndexType, ValueType>::readGraph( graphFile, vectorOfNodeWeights, settings.fileFormat );
        } else{
            graph = ITI::FileIO<IndexType, ValueType>::readGraph( graphFile, vectorOfNodeWeights );
        }
        N = graph.getNumRows();
        scai::dmemo::DistributionPtr rowDistPtr = graph.getRowDistributionPtr();
        scai::dmemo::DistributionPtr noDistPtr( new scai::dmemo::NoDistribution( N ));
        assert(graph.getColDistribution().isEqual(*noDistPtr));
        
        // for 2D we do not know the size of every dimension
        settings.numX = N;
        settings.numY = IndexType(1);
        settings.numZ = IndexType(1);
        
        //read the coordinates file
		if (vm.count("coordFormat")) {
			coordinates = ITI::FileIO<IndexType, ValueType>::readCoords(coordFile, N, settings.dimensions, coordFormat);
		}else if (vm.count("fileFormat")) {
            coordinates = ITI::FileIO<IndexType, ValueType>::readCoords(coordFile, N, settings.dimensions, settings.fileFormat);
        } else {
            coordinates = ITI::FileIO<IndexType, ValueType>::readCoords(coordFile, N, settings.dimensions);
        }
        PRINT0("read  graph and coordinates");        
        
        //unit weights
        scai::hmemo::HArray<ValueType> localWeights( rowDistPtr->getLocalSize(), 1 );
        nodeWeights.swap( localWeights, rowDistPtr );

        if (thisPE== 0) {
            std::cout << "Read " << N << " points." << std::endl;
            std::cout << "Read coordinates." << std::endl;
            std::cout << "On average there are about " << N/comm->getSize() << " points per PE."<<std::endl;
        }

	}
    else if(vm.count("generate")){
        if ( thisPE == 0){
            std::cout<< "input: generate" << std::endl;
        }
        if (settings.dimensions == 2) {
            settings.numZ = 1;
        }
        
        N = settings.numX * settings.numY * settings.numZ;
        	
		std::vector<ValueType> maxCoord(settings.dimensions); // the max coordinate in every dimensions
		
        maxCoord[0] = settings.numX;
        maxCoord[1] = settings.numY;
        maxCoord[2] = settings.numZ;
        
        std::vector<IndexType> numPoints(3); // number of points in each dimension, used only for 3D
        
        for (IndexType i = 0; i < 3; i++) {
            numPoints[i] = maxCoord[i];
        }
        
        if( thisPE== 0){
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
        
        if( thisPE==0 ){
            std::cout<< "Generated random 3D graph with "<< nodes<< " and "<< edges << " edges."<< std::endl;
        }
        
    }
    else{
    	std::cout << "Only input file as input. Call again with --graphFile" << std::endl;
    	return 0;
    }
       
    //
    //  read block sizes from a file if it is passed as an argument
    //
    if( vm.count("blockSizesFile") ){
        settings.blockSizes = ITI::FileIO<IndexType, ValueType>::readBlockSizes( blockSizesFile, settings.numBlocks );
        IndexType blockSizesSum  = std::accumulate( settings.blockSizes.begin(), settings.blockSizes.end(), 0);
        IndexType nodeWeightsSum = nodeWeights.sum().Scalar::getValue<IndexType>();
        SCAI_ASSERT_GE( blockSizesSum, nodeWeightsSum, "The block sizes provided are not enough to fit the total weight of the input" );
    }
    
    // time needed to get the input
    //std::chrono::duration<double> inputTime = std::chrono::system_clock::now() - startTime;

    assert(N > 0);

    if( !(vm.count("numBlocks") or vm.count("cutsPerDim")) ){
        settings.numBlocks = comm->getSize();
    }
    
    //----------
    
    scai::dmemo::DistributionPtr rowDistPtr = graph.getRowDistributionPtr();
    scai::dmemo::DistributionPtr noDistPtr( new scai::dmemo::NoDistribution( N ));
    
    //DenseVector<IndexType> uniformWeights;
    
    //settings.minGainForNextRound = IndexType(10);
    //settings.minBorderNodes = IndexType(10);
    settings.useGeometricTieBreaking = IndexType(1);
    settings.pixeledSideLen = IndexType ( std::min(settings.numBlocks, IndexType(100) ) );
        
    settings.print( std::cout , comm );
        
    IndexType dimensions = settings.dimensions;
    IndexType k = settings.numBlocks;
    
    std::chrono::time_point<std::chrono::system_clock> beforeInitialTime;
    std::chrono::duration<double> partitionTime;
    std::chrono::duration<double> finalPartitionTime;
    
    
    using namespace ITI;
    
	const IndexType rank = comm->getRank();
	const IndexType localN = rowDistPtr->getLocalSize();
    const IndexType globalN = rowDistPtr->getGlobalSize();
	
    if(comm->getRank()==0) std::cout <<std::endl<<std::endl;
    
    scai::lama::DenseVector<IndexType> partition;
    IndexType initialPartition = static_cast<IndexType> (settings.initialPartition);
    
    comm->synchronize();
    
    switch( initialPartition ){
        case 0:{  //------------------------------------------- hilbert/sfc
           
            beforeInitialTime =  std::chrono::system_clock::now();
            PRINT0( "Get a hilbert/sfc partition");
            
            // get a hilbertPartition
            partition = ParcoRepart<IndexType, ValueType>::hilbertPartition( coordinates, settings);
            
            partitionTime =  std::chrono::system_clock::now() - beforeInitialTime;
            
            // the hilbert partition internally sorts and thus redistributes the points
            //graph.redistribute( partition.getDistributionPtr() , noDistPtr );
            //rowDistPtr = graph.getRowDistributionPtr();
            
            assert( partition.size() == N);
            assert( coordinates[0].size() == N);
            
            break;
        }
        case 1:{  //------------------------------------------- pixeled
  
            beforeInitialTime =  std::chrono::system_clock::now();
            PRINT0( "Get a pixeled partition");
            
            // get a pixelPartition
            partition = ParcoRepart<IndexType, ValueType>::pixelPartition( coordinates, settings);
            
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
            PRINT0( "Get a k-means partition");
            
            // get a k-means partition
			IndexType weightSum;
			bool nodesUnweighted=true;
				
			int repeatTimes = 5;
			beforeInitialTime =  std::chrono::system_clock::now();

			for(int r=0 ; r< repeatTimes; r++){
				
				std::chrono::time_point<std::chrono::system_clock> beforeTmp = std::chrono::system_clock::now();
				
				DenseVector<IndexType> tempResult = ParcoRepart<IndexType, ValueType>::hilbertPartition(coordinates, settings);
				//std::vector<sort_pair> localHilbertIndices = ITI::HilbertCurve<IndexType, ValueType>::getSortedHilbertIndices( coordinates );
				
				std::chrono::duration<double> sfcPartTime = std::chrono::system_clock::now() - beforeTmp;
				ValueType time = comm->max( sfcPartTime.count() );
				PRINT0("time to get the sfc partition: " << time);
				
				std::chrono::time_point<std::chrono::system_clock> beforeRedist	=std::chrono::system_clock::now() ;
				
				
				if (nodeWeights.size() == 0) {
					nodeWeights = DenseVector<ValueType>(rowDistPtr, 1);
					nodesUnweighted = true;
				} else {
					nodesUnweighted = (nodeWeights.max() == nodeWeights.min());
				}
				const std::vector<IndexType> blockSizes(settings.numBlocks, weightSum/settings.numBlocks);
				
				
				std::vector<DenseVector<ValueType> > coordinateCopy = coordinates;
				DenseVector<ValueType> nodeWeightCopy = nodeWeights;

				{		// redistribute coordinates and nodeweigths block
/* // with a redistributor				
					scai::dmemo::Redistributor prepareRedist(  tempResult.getDistributionPtr() , coordinates[0].getDistributionPtr());
					std::chrono::duration<double> redistribTime1 = std::chrono::system_clock::now() - beforeRedist;
					time = comm->max( redistribTime1.count() );
					PRINT0("time to construct redistributor: " << time);
*/				

/* //with the DenseVector.redistribute();
					for (IndexType d = 0; d < dimensions; d++) {
						coordinateCopy[d].redistribute( tempResult.getDistributionPtr() );
						//coordinateCopy[d].redistribute(prepareRedist);
					}
*/

// with by-hand redistribution
					if (comm->getSize() > 1 && (settings.dimensions == 2 || settings.dimensions == 3)) {
						SCAI_REGION("ParcoRepart.partitionGraph.initialPartition.prepareForKMeans")
						Settings migrationSettings = settings;
						migrationSettings.numBlocks = comm->getSize();
						migrationSettings.epsilon = settings.epsilon;
						//migrationSettings.bisect = true;
						
						// the distribution for the initial migration   
						scai::dmemo::DistributionPtr initMigrationPtr;
						
						if (!settings.repartition || comm->getSize() != settings.numBlocks) {
							DenseVector<IndexType> tempResult;
							
							if (settings.initialMigration == InitialPartitioningMethods::SFC) {

								SCAI_REGION_START("ParcoRepart.partitionGraph.initialPartition.prepareForKMeans.sfc")
								/*
								* TODO:
								* after all the branches have been merged, replace the copy-pasted code with a call to
								* HilbertCurve<IndexType, ValueType>::getHilbertIndexVector(coordinates, settings.sfcResolution, settings.dimensions);
								*/
								std::vector<ValueType> hilbertIndices(localN);
								std::vector<std::vector<ValueType>> points(localN, std::vector<ValueType>(settings.dimensions,0));
								std::vector<ValueType> minCoords(settings.dimensions);
								std::vector<ValueType> maxCoords(settings.dimensions);

								for (IndexType d = 0; d < settings.dimensions; d++) {
									minCoords[d] = coordinates[d].min().Scalar::getValue<ValueType>();
									maxCoords[d] = coordinates[d].max().Scalar::getValue<ValueType>();
									assert(std::isfinite(minCoords[d]));
									assert(std::isfinite(maxCoords[d]));
									scai::hmemo::ReadAccess<ValueType> rCoords(coordinates[d].getLocalValues());
									for (IndexType i = 0; i < localN; i++) {
										points[i][d] = rCoords[i];
									}
								}

								for (IndexType i = 0; i < localN; i++) {
									hilbertIndices[i] = HilbertCurve<IndexType, ValueType>::getHilbertIndex(points[i].data(), settings.dimensions, settings.sfcResolution, minCoords, maxCoords);
								}

								/**
								* fill sort pair
								*/
								scai::utilskernel::LArray<IndexType> myGlobalIndices(localN, 0);
								rowDistPtr->getOwnedIndexes(myGlobalIndices);
								std::vector<sort_pair> localPairs(localN);
								{
									scai::hmemo::ReadAccess<IndexType> rIndices(myGlobalIndices);
									for (IndexType i = 0; i < localN; i++) {
										localPairs[i].value = hilbertIndices[i];
										localPairs[i].index = rIndices[i];
									}
								}

								SCAI_REGION_END("ParcoRepart.partitionGraph.initialPartition.prepareForKMeans.sfc")

								SCAI_REGION_START("ParcoRepart.partitionGraph.initialPartition.prepareForKMeans.sort")
								MPI_Comm mpi_comm = MPI_COMM_WORLD;//maybe cast the communicator ptr to a MPI communicator and get getMPIComm()?
								SQuick::sort<sort_pair>(mpi_comm, localPairs, -1);//could also do this with just the hilbert index - as a valueType
								//IndexType newLocalN = localPairs.size();

								//migrationCalculation = std::chrono::system_clock::now() - beforeInitPart;
								//metrics.timeMigrationAlgo[rank]  = migrationCalculation.count();

								std::chrono::time_point<std::chrono::system_clock> beforeMigration =  std::chrono::system_clock::now();

								assert(localPairs.size() > 0);
								sort_pair minLocalIndex = localPairs[0];

								std::vector<ValueType> sendThresholds(comm->getSize(), minLocalIndex.value);
								std::vector<ValueType> recvThresholds(comm->getSize());

								static_assert(std::is_same<ValueType, double>::value, "assuming ValueType double. Change this assertion and following line.");
								MPI_Datatype MPI_ValueType = MPI_DOUBLE; //TODO: properly template this

								PRINT0("Setting local threshold to " + std::to_string(minLocalIndex.value));

								SCAI_REGION_END("ParcoRepart.partitionGraph.initialPartition.prepareForKMeans.sort")

								MPI_Alltoall(sendThresholds.data(), 1,  MPI_ValueType,
												recvThresholds.data(), 1, MPI_ValueType,
												mpi_comm);//TODO: replace this monstrosity with a proper call to LAMA
								//comm->all2all(recvThresholds.data(), sendTresholds.data());//TODO: maybe speed up with hypercube

								PRINT0(std::to_string(recvThresholds[0]) + " < hilbert indices < " + std::to_string(recvThresholds[comm->getSize()-1]) + ".");


								// merge to get quantities //Problem: nodes are not sorted according to their hilbert indices, so accesses are not aligned.
								// Need to sort before and after communication
								assert(std::is_sorted(recvThresholds.begin(), recvThresholds.end()));

								std::vector<IndexType> permutation(localN);
								std::iota(permutation.begin(), permutation.end(), 0);
								std::sort(permutation.begin(), permutation.end(), [&](IndexType i, IndexType j){return hilbertIndices[i] < hilbertIndices[j];});

								SCAI_REGION_START("ParcoRepart.partitionGraph.initialPartition.prepareForKMeans.communicationPlan")
								//now sorting hilbert indices themselves
								std::sort(hilbertIndices.begin(), hilbertIndices.end());

								std::vector<IndexType> quantities(comm->getSize(), 0);

								{
									IndexType p = 0;
									for (IndexType i = 0; i < localN; i++) {
										//increase target block counter if threshold is reached. Skip empty blocks if necessary.
										while (p+1 < comm->getSize() && recvThresholds[p+1] <= hilbertIndices[i]) {//TODO: check this more thoroughly when you are awake
											p++;
										}
										assert(p < comm->getSize());

										quantities[p]++;
									}
								}

								// allocate sendPlan
								scai::dmemo::CommunicationPlan sendPlan(quantities.data(), comm->getSize());
								SCAI_ASSERT_EQ_ERROR(sendPlan.totalQuantity(), localN, "wrong size of send plan")

								// allocate recvPlan - either with allocateTranspose, or directly
								scai::dmemo::CommunicationPlan recvPlan;
								recvPlan.allocateTranspose(sendPlan, *comm);
								SCAI_REGION_END("ParcoRepart.partitionGraph.initialPartition.prepareForKMeans.communicationPlan")

								IndexType newLocalN = recvPlan.totalQuantity();
								//SCAI_ASSERT_EQ_ERROR(recvPlan.totalQuantity(), newLocalN, "wrong size of recv plan");
								PRINT0(std::to_string(localN) + " old local values " + std::to_string(newLocalN) + " new ones.");


								//transmit indices, allowing for resorting of the received values
								std::vector<IndexType> sendIndices(localN);
								{
									SCAI_REGION("ParcoRepart.partitionGraph.initialPartition.prepareForKMeans.permute");
									scai::hmemo::ReadAccess<IndexType> rIndices(myGlobalIndices);
									for (IndexType i = 0; i < localN; i++) {
										assert(permutation[i] < localN);
										assert(permutation[i] >= 0);
										sendIndices[i] = rIndices[permutation[i]];
									}
								}
								std::vector<IndexType> recvIndices(newLocalN);
								comm->exchangeByPlan(recvIndices.data(), recvPlan, sendIndices.data(), sendPlan);

								//get new distribution
								scai::utilskernel::LArray<IndexType> indexTransport(newLocalN, recvIndices.data());

								scai::dmemo::DistributionPtr newDist(new scai::dmemo::GeneralDistribution(globalN, indexTransport, comm));
								SCAI_ASSERT_EQUAL(newDist->getLocalSize(), newLocalN, "wrong size of new distribution");

								for (IndexType i = 0; i < newLocalN; i++) {
									SCAI_ASSERT_VALID_INDEX_DEBUG(recvIndices[i], globalN, "invalid index");
								}

								PRINT0("Exchanged indices.");

								{
									SCAI_REGION("ParcoRepart.partitionGraph.initialPartition.prepareForKMeans.redistribute");
									// for each dimension: define DenseVector with new distribution, get write access to local values, call exchangeByPlan
									std::vector<ValueType> sendBuffer(localN);
									std::vector<ValueType> recvBuffer(newLocalN);

									PRINT0("Allocated Buffers.");

									for (IndexType d = 0; d < settings.dimensions; d++) {
										scai::hmemo::ReadAccess<ValueType> rCoords(coordinates[d].getLocalValues());
										{
											SCAI_REGION("ParcoRepart.partitionGraph.initialPartition.prepareForKMeans.redistribute.permute");
											for (IndexType i = 0; i < localN; i++) {//TODO:maybe extract into lambda?
												sendBuffer[i] = rCoords[permutation[i]];//TODO: how to make this more cache-friendly? (Probably by using pairs and sorting them.)
											}
										}
										PRINT0("Filled send buffer for coordinates of axis " + std::to_string(d));

										comm->exchangeByPlan(recvBuffer.data(), recvPlan, sendBuffer.data(), sendPlan);
										PRINT0("Exchanged coordinates for axis " + std::to_string(d));
										coordinateCopy[d] = DenseVector<ValueType> (newDist, 0);
										{
											SCAI_REGION("ParcoRepart.partitionGraph.initialPartition.prepareForKMeans.redistribute.permute");
											scai::hmemo::WriteAccess<ValueType> wCoords(coordinateCopy[d].getLocalValues());
											assert(wCoords.size() == newLocalN);
											for (IndexType i = 0; i < newLocalN; i++) {
												wCoords[newDist->global2local(recvIndices[i])] = recvBuffer[i];
											}
										}
										PRINT0("Permuted received coordinates for axis " + std::to_string(d));
									}

									// same for node weights
									nodeWeightCopy = DenseVector<ValueType> (newDist, 1);
									if (!nodesUnweighted) {
										scai::hmemo::ReadAccess<ValueType> rWeights(nodeWeights.getLocalValues());
										{
											SCAI_REGION("ParcoRepart.partitionGraph.initialPartition.prepareForKMeans.redistribute.permute");
											for (IndexType i = 0; i < localN; i++) {
												sendBuffer[i] = rWeights[permutation[i]];//TODO: how to make this more cache-friendly? (Probably by using pairs and sorting them.)
											}
										}

										comm->exchangeByPlan(recvBuffer.data(), recvPlan, sendBuffer.data(), sendPlan);
										{
											SCAI_REGION("ParcoRepart.partitionGraph.initialPartition.prepareForKMeans.redistribute.permute");
											scai::hmemo::WriteAccess<ValueType> wWeights(nodeWeightCopy.getLocalValues());
											for (IndexType i = 0; i < newLocalN; i++) {
												wWeights[newDist->global2local(recvIndices[i])] = recvBuffer[i];
											}
										}
									}

									PRINT0("Exchanged weights.");
								}

								//migrationTime = std::chrono::system_clock::now() - beforeMigration;
								//metrics.timeFirstDistribution[rank]  = migrationTime.count();

							}
						}
					
					}	

				}
				std::chrono::duration<double> redistribTime = std::chrono::system_clock::now() - beforeRedist;
				time = comm->max( redistribTime.count() );
				PRINT0("time to redistribute coordinates: " << time);
				
				//
				const IndexType localN = graph.getLocalNumRows();
				settings.minSamplingNodes = std::max<IndexType>( IndexType(500), N/(k*20) );
				//settings.minSamplingNodes = localN;
				//
				
				partition = ITI::KMeans::computePartition(coordinateCopy, settings.numBlocks, nodeWeightCopy, blockSizes, settings);     
				std::chrono::duration<double> thisPartitionTime = std::chrono::system_clock::now() - beforeTmp;
				time  = comm->max( thisPartitionTime.count() );
				PRINT0("Time for run " << r << " is " << time);
			}
				
			partitionTime =  (std::chrono::system_clock::now() - beforeInitialTime)/repeatTimes;
				
			// the hilbert partition internally sorts and thus redistributes the points
			//graph.redistribute( partition.getDistributionPtr() , noDistPtr );
            //rowDistPtr = graph.getRowDistributionPtr();
			
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
            break;   
        }
        default:{
            PRINT0("Value "<< initialPartition << " for option initialPartition not supported" );
            break;
        }
    }
    
    partition.redistribute(  graph.getRowDistributionPtr() );
    // partition has the the same distribution as the graph rows 
	SCAI_ASSERT_ERROR( partition.getDistribution().isEqual( graph.getRowDistribution() ), "Distribution mismatch.")
    
    //
    // Get metrics
    //
    
    struct Metrics metrics(1);
	metrics.numBlocks = settings.numBlocks;
	
	metrics.timeFinalPartition = comm->max( partitionTime.count() );
	metrics.getAllMetrics( graph, partition, nodeWeights, settings );
	
	//
    // Reporting output to std::cout and/or outFile
	//
	
    if ( thisPE == 0) {
		//metrics.print( std::cout );
		std::cout << "Running " << __FILE__ << std::endl;
		printMetricsShort( metrics, std::cout);
		// write in a file
        if( settings.outFile!="-" ){
			std::ofstream outF( settings.outFile, std::ios::out);
			if(outF.is_open()){
				outF << "Running " << __FILE__ << std::endl;
                if( vm.count("generate") ){
                    outF << "machine:" << machine << " input: generated mesh,  nodes:" << N << " epsilon:" << settings.epsilon<< std::endl;
                }else{
                    outF << "machine:" << machine << " input: " << vm["graphFile"].as<std::string>() << " nodes:" << N << " epsilon:" << settings.epsilon<< std::endl;
                }
                settings.print( outF, comm );

                //metrics.print( outF ); 
				printMetricsShort( metrics, outF);
                std::cout<< "Output information written to file " << settings.outFile << std::endl;
            }else{
                std::cout<< "Could not open file " << settings.outFile << " informations not stored"<< std::endl;
            } 
		}
		
    }
    
    // the code below writes the output coordinates in one file per processor for visualization purposes.
    //=================
    
    if (settings.writeDebugCoordinates) {
        
        if(comm->getSize() != k){
            PRINT("Cannot print local coords into file as numBlocks must be equal numPEs.");
            return 0;
        }
        /**
         * redistribute so each PE writes its block
         */
        scai::dmemo::DistributionPtr newDist( new scai::dmemo::GeneralDistribution ( *rowDistPtr, partition.getLocalValues() ) );
        assert(newDist->getGlobalSize() == N);
        partition.redistribute( newDist);
        
        for (IndexType d = 0; d < dimensions; d++) {
            coordinates[d].redistribute(newDist);  
            assert( coordinates[d].size() == N);
            assert( coordinates[d].getLocalValues().size() == newDist->getLocalSize() );
        }

        std::string destPath = "partResults/testInitial_"+std::to_string(initialPartition) +"/blocks_" + std::to_string(settings.numBlocks) ;
        
        boost::filesystem::create_directories( destPath );   
        ITI::FileIO<IndexType, ValueType>::writeCoordsDistributed( coordinates, N, dimensions, destPath + "/debugResult");
    }
    
    if( writePartition ){
        std::string partOutFile;
        if( settings.outFile!="-" ){
            partOutFile = settings.outFile + ".partition";
        }else if( vm.count("graphFile") ){
            partOutFile = settings.fileName + ".partition";
        }else if( vm.count("generate") ){
            partOutFile = "generate_"+ std::to_string(settings.numX)+ ".partition";
        }
        // write partition in file
        ITI::FileIO<IndexType, ValueType>::writePartitionCentral( partition, partOutFile );        
    }
    
    std::chrono::duration<ValueType> totalTimeLocal = std::chrono::system_clock::now() - startTime;
	ValueType totalTime = comm->max( totalTimeLocal.count() );
	if( thisPE==0 ){
		std::cout<<"Exiting file " << __FILE__ << " , total time= " << totalTime <<  std::endl;
	}
	
    std::exit(0);
	return 0;
}
