#include "VariableWatcher.hpp"

struct MyStruct
{
	int m_i;
	float m_f;
};

std::string to_string(const MyStruct& oStruct)
{
	return { "m_i : " + std::to_string(oStruct.m_i) + " - m_f : " + std::to_string(oStruct.m_f) };
}

void Test()
{
	VariableWatcher::Watcher<int> iTest("iTest");

	iTest = 8;

	int* pPtr = &iTest;
	*pPtr = 42;

	VariableWatcher::Watcher<float> fTest("fTest", 5.3f);

	fTest = 10.2f;

	float* pPtrFloat = &fTest;
	*pPtrFloat = 4.2f;

	VariableWatcher::Watcher<MyStruct> oTest("oTest");

	MyStruct& oRefTest = oTest;
	oRefTest.m_i = 12;
	oRefTest.m_f = 1.6f;

	pPtr = &oRefTest.m_i;
	*pPtr = 33;
}