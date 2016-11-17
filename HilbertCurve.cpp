/*
 * ParcoReportHilbert.cpp
 *
 *  Created on: 15.11.2016
 *      Author: tzovas
 */

#include <scai/dmemo/Distribution.hpp>

#include <assert.h>
#include <cmath>
#include <climits>
#include <queue>

#include "PrioQueue.h"
#include "ParcoRepart.h"
#include "HilbertCurve.h"

namespace ITI{

/**
* possible optimization: check whether all local points lie in the same region and thus have a common prefix
*/

template<typename IndexType, typename ValueType>
ValueType HilbertCurve<IndexType, ValueType>::getHilbertIndex(const DenseVector<ValueType> &coordinates, IndexType dimensions, IndexType index, IndexType recursionDepth,
	const std::vector<ValueType> &minCoords, const std::vector<ValueType> &maxCoords) {

	if (dimensions > 3 || dimensions < 2) {
		throw std::logic_error("Space filling curve currently only implemented for two or three dimensions");
	}

	scai::dmemo::DistributionPtr coordDist = coordinates.getDistributionPtr();

	if (coordDist->getLocalSize() % int(dimensions) != 0) {
		throw std::runtime_error("Size of coordinate vector no multiple of dimension. Maybe it was split in the distribution?");
	}

	size_t bitsInValueType = sizeof(ValueType) * CHAR_BIT;
	if (recursionDepth > bitsInValueType/dimensions) {
		throw std::runtime_error("A space-filling curve that precise won't fit into the return datatype.");
	}

	if (!coordDist->isLocal(index*dimensions)) {
		throw std::runtime_error("Coordinate with index " + std::to_string(index) + " is not present on this process.");
	}

	const scai::utilskernel::LArray<ValueType>& myCoords = coordinates.getLocalValues();
	std::vector<ValueType> scaledCoord(dimensions);

	for (IndexType dim = 0; dim < dimensions; dim++) {
		assert(coordDist->isLocal(index*dimensions+dim));
		const Scalar coord = myCoords[coordDist->global2local(index*dimensions+dim)];
		scaledCoord[dim] = (coord.getValue<ValueType>() - minCoords[dim]) / (maxCoords[dim] - minCoords[dim]);
		if (scaledCoord[dim] < 0 || scaledCoord[dim] > 1) {
			throw std::runtime_error("Coordinate " + std::to_string(coord.getValue<ValueType>()) + " at position " 
				+ std::to_string(index*dimensions + dim) + " does not agree with bounds "
				+ std::to_string(minCoords[dim]) + " and " + std::to_string(maxCoords[dim]));
		}
	}

   if(dimensions==2){
	double temp=1;
	long integerIndex = 0;//TODO: also check whether this data type is long enough
	for (IndexType i = 0; i < recursionDepth; i++) {
		int subSquare;
		//two dimensions only, for now
		if (scaledCoord[0] < 0.5) {
			if (scaledCoord[1] < 0.5) {
				subSquare = 0;
				//apply inverse hilbert operator
				temp = scaledCoord[0];
				scaledCoord[0] = 2*scaledCoord[1];
				scaledCoord[1] = 2*temp;
			} else {
				subSquare = 1;
				//apply inverse hilbert operator
				scaledCoord[0] *= 2;
				scaledCoord[1] = 2*scaledCoord[1] -1;
			}
		} else {
			if (scaledCoord[1] < 0.5) {
				subSquare = 3;
				//apply inverse hilbert operator
				temp = scaledCoord[0];
				scaledCoord[0] = -2*scaledCoord[1]+1;
				scaledCoord[1] = -2*temp+2;

			} else {
				subSquare = 2;
				//apply inverse hilbert operator
				scaledCoord[0] = 2*scaledCoord[0]-1;
				scaledCoord[1] = 2*scaledCoord[1]-1;
			}
		}
		integerIndex = (integerIndex << 2) | subSquare;	
	}
	long divisor = 1 << (2*int(recursionDepth));
	double ret = double(integerIndex) / double(divisor);
	return ret; 
   }else
	return HilbertCurve<IndexType, ValueType>::getHilbertIndex3D(coordinates, dimensions, index, recursionDepth ,minCoords, maxCoords);
}

//-------------------------------------------------------------------------------------------------

template<typename IndexType, typename ValueType>
DenseVector<ValueType> HilbertCurve<IndexType, ValueType>::Hilbert2DIndex2Point(ValueType index, IndexType level){
	DenseVector<ValueType>  p(2,0), ret(2,0);
	ValueType r;
	IndexType q;

	if(level==0)
		return ret;
	else{
		q=int(4*index);
    		r= 4*index-q;
		p = HilbertCurve<IndexType, ValueType>::Hilbert2DIndex2Point(r, level-1);
		switch(q){
			case 0: ret.setValue(0, p(1)/2);	ret.setValue(1, p(0)/2);	return ret;
			case 1: ret.setValue(0, p(0)/2);	ret.setValue(1, p(1)/2 +0.5);	return ret;
			case 2: ret.setValue(0, p(0)/2 +0.5);	ret.setValue(1, p(1)/2 +0.5);	return ret;
			case 3: ret.setValue(0, 1-p(1)/2);	ret.setValue(1, 0.5-p(0)/2);	return ret;
		}
	}
	return ret;
}

//-------------------------------------------------------------------------------------------------
/**
* Given a point in 3D it returns its hilbert index, a value in [0,1]. 
**/
template<typename IndexType, typename ValueType>
ValueType HilbertCurve<IndexType, ValueType>::getHilbertIndex3D(const DenseVector<ValueType> &coordinates, IndexType dimensions, IndexType index, IndexType recursionDepth,
	const std::vector<ValueType> &minCoords, const std::vector<ValueType> &maxCoords) {

	if (dimensions != 3) {
		throw std::logic_error("Space filling curve for 3 dimensions.");
	}

	scai::dmemo::DistributionPtr coordDist = coordinates.getDistributionPtr();

	if (coordDist->getLocalSize() % int(dimensions) != 0) {
		throw std::runtime_error("Size of coordinate vector no multiple of dimension. Maybe it was split in the distribution?");
	}

	size_t bitsInValueType = sizeof(ValueType) * CHAR_BIT;
	if (recursionDepth > bitsInValueType/dimensions) {
		throw std::runtime_error("A space-filling curve that precise won't fit into the return datatype.");
	}

	if (!coordDist->isLocal(index*dimensions)) {
		throw std::runtime_error("Coordinate with index " + std::to_string(index) + " is not present on this process.");
	}

	const scai::utilskernel::LArray<ValueType>& myCoords = coordinates.getLocalValues();
	std::vector<ValueType> scaledCoord(dimensions);

	for (IndexType dim = 0; dim < dimensions; dim++) {
		assert(coordDist->isLocal(index*dimensions+dim));
		const Scalar coord = myCoords[coordDist->global2local(index*dimensions+dim)];
		scaledCoord[dim] = (coord.getValue<ValueType>() - minCoords[dim]) / (maxCoords[dim] - minCoords[dim]);
		if (scaledCoord[dim] < 0 || scaledCoord[dim] > 1) {
			throw std::runtime_error("Coordinate " + std::to_string(coord.getValue<ValueType>()) + " at position " 
				+ std::to_string(index*dimensions + dim) + " does not agree with bounds "
				+ std::to_string(minCoords[dim]) + " and " + std::to_string(maxCoords[dim]));
		}
	}
	
	ValueType tmpX, tmpY, tmpZ;
	ValueType x ,y ,z; 	//the coordinates each of the three dimensions
	x= scaledCoord[0];
	y= scaledCoord[1];
	z= scaledCoord[2];
	long integerIndex = 0;	//TODO: also check whether this data type is long enough

	for (IndexType i = 0; i < recursionDepth; i++) {
		int subSquare;
		if (z < 0.5) {
			if (x < 0.5) {
				if (y <0.5){		//x,y,z <0.5
					subSquare= 0;
					//apply inverse hilbert operator
					tmpX= x;
					x= 2*z;
					z= 2*y;
					y= 2*tmpX;
				} else{			//z<0.5, y>0.5, x<0.5
					subSquare= 1;
					tmpX= x;
					x= 2*y-1;
					y= 2*z;
					z= 2*tmpX;
				}
			} else if (y>=0.5){		//z<0.5, y,x>0,5
					subSquare= 2;
					//apply inverse hilbert operator
					tmpX= x;					
					x= 2*y-1;
					y= 2*z;
					z= 2*tmpX-1;
				}else{			//z<0.5, y<0.5, x>0.5
					subSquare= 3;
					x= -2*x+2;
					y= -2*y+1;
					z= 2*z;
				}
		} else if(x>=0.5){
				if(y<0.5){ 		//z>0.5, y<0.5, x>0.5
					subSquare= 4;
					x= -2*x+2;
					y= -2*y+1;
					z= 2*z-1;
				} else{			//z>0.5, y>0.5, x>0.5
					subSquare= 5;
					tmpX= x;
					x= 2*y-1;
					y= -2*z+2;
					z= -2*tmpX+2;				
				}
			}else if(y<0.5){		//z>0.5, y<0.5, x<0.5
					subSquare= 7;	//care, this is 7, not 6	
					tmpX= x;
					x= -2*z+2;
					z= -2*y+1;
					y= 2*tmpX;				
				}else{			//z>0.5, y>0.5, x<0.5
					subSquare= 6;	//this is case 6
					tmpX= x;
					x= 2*y-1;
					y= -2*z +2;
					z= -2*tmpX+1;				
				}
		integerIndex = (integerIndex << 3) | subSquare;		
	}
	long divisor = 1 << (3*int(recursionDepth));
	double ret = double(integerIndex) / double(divisor);
	return ret; 

}
//-------------------------------------------------------------------------------------------------
/*
* Given a 3D point it returns its index in [0,1] on the hilbert curve based on the level depth.
*/

template<typename IndexType, typename ValueType>
DenseVector<ValueType> HilbertCurve<IndexType, ValueType>::Hilbert3DIndex2Point(ValueType index, IndexType level){
	DenseVector<ValueType>  p(3,0), ret(3,0);
	ValueType r;
	IndexType q;
	
	if(level==0)
		return ret;
	else{		
		q=int(8*index); 
    		r= 8*index-q;
		if( (q==0) && r==0 ) return ret;
		p = HilbertCurve<IndexType, ValueType>::Hilbert3DIndex2Point(r, level-1);

		switch(q){
			case 0: ret.setValue(0, p(1)/2);	ret.setValue(1, p(2)/2);	ret.setValue(2, p(0)/2);	return ret;
			case 1: ret.setValue(0, p(2)/2);	ret.setValue(1, 0.5+p(0)/2);	ret.setValue(2, p(1)/2);	return ret;
			case 2: ret.setValue(0, 0.5+p(2)/2);	ret.setValue(1, 0.5+p(0)/2);	ret.setValue(2, p(1)/2);	return ret;
			case 3: ret.setValue(0, 1-p(0)/2);	ret.setValue(1, 0.5-p(1)/2);	ret.setValue(2, -p(2)/2);	return ret;
			case 4: ret.setValue(0, 1-p(0)/2);	ret.setValue(1, 0.5-p(1)/2);	ret.setValue(2, 0.5+p(2)/2);	return ret;
			case 5: ret.setValue(0, 1-p(2)/2);	ret.setValue(1, 0.5+p(0)/2);	ret.setValue(2, 1-p(1)/2);	return ret;
			case 6: ret.setValue(0, 0.5-p(2)/2);	ret.setValue(1, 0.5+p(0)/2);	ret.setValue(2, 1-p(1)/2);	return ret;
			case 7: ret.setValue(0, p(1)/2);	ret.setValue(1, 0.5-p(2)/2);	ret.setValue(2, 1-p(0)/2);	return ret;			
		}
	}
	return ret;
}

//-------------------------------------------------------------------------------------------------

template double HilbertCurve<int, double>::getHilbertIndex(const DenseVector<double> &coordinates, int dimensions, int index, int recursionDepth,
	const std::vector<double> &minCoords, const std::vector<double> &maxCoords);

template double HilbertCurve<int, double>::getHilbertIndex3D(const DenseVector<double> &coordinates, int dimensions, int index, int recursionDepth,
	const std::vector<double> &minCoords, const std::vector<double> &maxCoords);

template DenseVector<double> HilbertCurve<int, double>::Hilbert2DIndex2Point(double index, int level);

template DenseVector<double> HilbertCurve<int, double>::Hilbert3DIndex2Point(double index, int level);

} //namespace ITI