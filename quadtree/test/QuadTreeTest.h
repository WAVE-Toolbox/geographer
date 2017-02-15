/*
 * QuadTreeTest.h
 *
 *  Created on: 28.05.2014
 *      Author: Moritz v. Looz (moritz.looz-corswarem@kit.edu)
 */

#pragma once

#include <gtest/gtest.h>
#include <cmath>
#include <vector>
#include <memory>

#include "../SpatialCell.h"

namespace ITI {

class QuadTreeTest: public testing::Test {
public:
	QuadTreeTest() = default;
	virtual ~QuadTreeTest() = default;

protected:
	template <class T>
	std::vector<std::shared_ptr<SpatialCell<T> > > getChildren(std::shared_ptr<SpatialCell<T> > node) {
		return node->children;
	}
};

} /* namespace ITI */
