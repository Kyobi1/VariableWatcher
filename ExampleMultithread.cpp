#include "VariableWatcher.hpp"
#include <thread>

struct MyStruct
{
	int m_i;
	float m_f;
};

std::string to_string(const MyStruct& oStruct)
{
	return { "m_i : " + std::to_string(oStruct.m_i) + " - m_f : " + std::to_string(oStruct.m_f) };
}

void Test3()
{
	VariableWatcher::Watcher<MyStruct> oTest("oTest");

	MyStruct& oRefTest = oTest;
	oRefTest.m_i = 12;
	oRefTest.m_f = 1.6f;

	int* pPtr = pPtr = &oRefTest.m_i;
	*pPtr = 33;
}

void Test2()
{
	VariableWatcher::Watcher<float> fTest("fTest", 5.3f);

	fTest = 10.2f;

	float* pPtrFloat = &fTest;
	*pPtrFloat = 4.2f;

	Test3();
}

void Test(int& iParam)
{
	VariableWatcher::Watcher<int> iTest("iTest");

	iTest = 8;

	int* pPtr = &iTest;
	*pPtr = 42;

	++iParam;

	Test2();
}

int main()
{
	VariableWatcher::Watcher<int> iTestParam("iTestParam", 6);
	std::thread oThread1(Test, iTestParam);
	Test(iTestParam);
	oThread1.join();
	return 0;
}