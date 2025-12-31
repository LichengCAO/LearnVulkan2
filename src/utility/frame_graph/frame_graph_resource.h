#pragma once
#include "common.h"
//class A {
//public:
//	A(std::function<void()> lambda) { m_f = lambda; }
//	void call() const { m_f(); } // Calls the stored lambda
//private:
//	std::function<void()> m_f;
//};
//
//int main() {
//	auto sharedLambda = []() { static int a = 0; ++a; std::cout << "shared: " << a << "\n"; };
//
//	A a1(sharedLambda);
//	A a2(sharedLambda);
//
//	a1.call(); // shared: 1
//	a2.call(); // shared: 2
//	a1.call(); // shared: 3
//	a2.call(); // shared: 4
//
//	return 0;
//}
//
//#include <iostream>
//#include <functional>
//
//class A {
//public:
//	void setLambda(std::function<void()> lambda) { m_f = lambda; }
//	void call() const { m_f(); } // Calls the stored lambda
//private:
//	std::function<void()> m_f;
//};
//
//int main() {
//	A a1, a2;
//
//	a1.setLambda([]() { static int a = 0; ++a; std::cout << "Lambda in a1: " << a << "\n"; });
//	a2.setLambda([]() { static int a = 0; ++a; std::cout << "Lambda in a2: " << a << "\n"; });
//
//	a1.call(); // Lambda in a1: 1
//	a2.call(); // Lambda in a2: 1
//	a1.call(); // Lambda in a1: 2
//	a2.call(); // Lambda in a2: 2
//
//	return 0;
//}

struct BufferResourceHandle
{
	uint32_t handle;
};

struct ImageResourceHandle
{
	uint32_t handle;
};

struct ImageResourceState
{
	VkImageSubresourceRange range;
	VkImageLayout layout;
	uint32_t queueFamily;
	VkAccessFlags availableAccess; // when does output ready
};

struct BufferResourceState
{
	VkDeviceSize offset;
	VkDeviceSize range;
	uint32_t queueFamily;
	VkAccessFlags availableAccess;
};