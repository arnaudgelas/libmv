// Copyright (c) 2009 libmv authors.
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.

#include "libmv/base/vector.h"
#include "libmv/numeric/numeric.h"
#include "testing/testing.h"

namespace {
using namespace libmv;

// This uses a Vec2d which is a fixed-size vectorizable Eigen type. It is
// necessary to test vectorizable types to ensure that the alignment asserts
// trigger if the alignment is not correct.
TEST(VectorAlignmentTest, PushBack) {
  Vec2 x1, x2;
  x1 << 1, 2;
  x2 << 3, 4;

  vector<Vec2> vs;
  vs.push_back(x1);
  EXPECT_EQ(1, vs.size());
  EXPECT_EQ(1, vs.capacity());

  vs.push_back(x2);
  EXPECT_EQ(2, vs.size());
  EXPECT_EQ(2, vs.capacity());
  EXPECT_EQ(x1, vs[0]);
  EXPECT_EQ(x2, vs[1]);

  vs.push_back(x2);
  vs.push_back(x2);
  vs.push_back(x2);
  EXPECT_EQ(5, vs.size());
  EXPECT_EQ(8, vs.capacity());
}

// Count the number of destruct calls to test that the destructor gets called.
int foo_construct_calls = 0;
int foo_destruct_calls = 0;

struct Foo {
 public:
  Foo() : value(5) { foo_construct_calls++; }
  ~Foo()           { foo_destruct_calls++;  }
  int value;
};

struct VectorTest : public testing::Test {
  VectorTest() {
    foo_construct_calls = 0;
    foo_destruct_calls = 0;
  }
};

TEST_F(VectorTest, EmptyVectorDoesNotConstruct) {
  {
    vector<Foo> v;
    EXPECT_EQ(0, v.size());
    EXPECT_EQ(0, v.capacity());
  }
  EXPECT_EQ(0, foo_construct_calls);
  EXPECT_EQ(0, foo_destruct_calls);
}

TEST_F(VectorTest, DestructorGetsCalled) {
  {
    vector<Foo> v;
    v.resize(5);
  }
  EXPECT_EQ(5, foo_construct_calls);
  EXPECT_EQ(5, foo_destruct_calls);
}

TEST_F(VectorTest, ReserveDoesNotCallConstructorsOrDestructors) {
  vector<Foo> v;
  EXPECT_EQ(0, v.size());
  EXPECT_EQ(0, v.capacity());
  EXPECT_EQ(0, foo_construct_calls);
  EXPECT_EQ(0, foo_destruct_calls);

  v.reserve(5);
  EXPECT_EQ(0, v.size());
  EXPECT_EQ(5, v.capacity());
  EXPECT_EQ(0, foo_construct_calls);
  EXPECT_EQ(0, foo_destruct_calls);
}

TEST_F(VectorTest, ResizeConstructsAndDestructsAsExpected) {
  vector<Foo> v;

  // Create one object.
  v.resize(1);
  EXPECT_EQ(1, v.size());
  EXPECT_EQ(1, v.capacity());
  EXPECT_EQ(1, foo_construct_calls);
  EXPECT_EQ(0, foo_destruct_calls);
  EXPECT_EQ(5, v[0].value);

  // Create two more.
  v.resize(3);
  EXPECT_EQ(3, v.size());
  EXPECT_EQ(3, v.capacity());
  EXPECT_EQ(3, foo_construct_calls);
  EXPECT_EQ(0, foo_destruct_calls);

  // Delete the last one.
  v.resize(2);
  EXPECT_EQ(2, v.size());
  EXPECT_EQ(3, v.capacity());
  EXPECT_EQ(3, foo_construct_calls);
  EXPECT_EQ(1, foo_destruct_calls);

  // Delete the remaining two.
  v.resize(0);
  EXPECT_EQ(0, v.size());
  EXPECT_EQ(3, v.capacity());
  EXPECT_EQ(3, foo_construct_calls);
  EXPECT_EQ(3, foo_destruct_calls);
}

TEST_F(VectorTest, PushPopBack) {
  vector<Foo> v;

  Foo foo;
  foo.value = 10;
  v.push_back(foo);
  EXPECT_EQ(1, v.size());
  EXPECT_EQ(10, v.back().value);

  v.pop_back();
  EXPECT_EQ(0, v.size());
  EXPECT_EQ(1, foo_construct_calls);
  EXPECT_EQ(1, foo_destruct_calls);
}

}  // namespace