# VariableWatcher
C++ tracker for variable values

This repo contains two source files :
<ul>
<li>VariableWatcher.hpp which is the only file you need to include to include in your project to use the watchers.</li>
<li>Example.cpp which contains examples showing how the watcher can be used. </li>
</ul>

<h2>Goals</h2>
This variable watcher system intends to allow you to track variable values during their lifetime. It was thought to be used mainly with primary types and small data objects and it's main purpose is for debugging a variable in a context where it's difficult to use a debugger.

<h2>Limitations</h2>
This system in currently working only on windows since it's using windows API for virtual allocations.
Please note that it is not currently thread safe and that if you want to track a structure bigger than your system page size, you will have a bad_alloc exception.
Since this system takes a lot of memory, I choosed to limit the number of simultaneous living watchers to the numbers specified in WatchersManager::s_uNbWatchers. Feel free to change it but keep in mind that you will use a lot more memory than needed for your tracked variables.
Using breakpoints and moving step by step in the code when a watched variable is used may causes undefined behavior on it because the debugger may catch events used by the system, so you should limit the use of breakpoints when using this.

<h2>How to use</h2>
Replace your variable from this
<br />
<br />
<code>int iMyVar = 5;</code>
<br />
<br />
to this :
<br />
<br />
<code>VariableWatcher::Watcher< int > iMyVar("iMyVar", 5);</code>
<br />
<br />
By default, you will have the following string output :
<pre>
Create watched var iMyVar with value 5
</pre>
If you have enabled the PRINT_CALLSTACK option, the string corresponding to the callstack will be added (this is a possible example) :
<pre>
Callstack :
D:\Visual Studio\Tests\Tests\VariableWatcher\VariableWatcher.hpp:VariableWatcher::Watcher<int>::Watcher<int> 405
D:\Visual Studio\Tests\Tests\main.cpp:Test 40
D:\Visual Studio\Tests\Tests\main.cpp:main 52
D:\a\_work\1\s\src\vctools\crt\vcstartup\src\startup\exe_common.inl:invoke_main 79
D:\a\_work\1\s\src\vctools\crt\vcstartup\src\startup\exe_common.inl:__scrt_common_main_seh 288
D:\a\_work\1\s\src\vctools\crt\vcstartup\src\startup\exe_common.inl:__scrt_common_main 331
D:\a\_work\1\s\src\vctools\crt\vcstartup\src\startup\exe_main.cpp:mainCRTStartup 17
BaseThreadInitThunk
RtlUserThreadStart
</pre>
For primitive types, you can then use it the same way as before.
<br />
<br />
For accessing data in custom types, you can take a reference on it :
<br />
<br />
<pre>
<code>VariableWatcher::Watcher< MyStruct > oMyStruct("oMyStruct");
MyStruct& oRefMyStruct = oMyStruct;</code>
</pre>

<h3>Custom logs</h3>
By default, the watchers will log by using the C++ standard output cout. You can disable this behavior by removing USE_COUT_FOR_DEFAULT_LOG symbol, which will make you use printf instead (if you don't want to include iostream header for example).
You can also set your own custom log function like this :
<pre>
<code>
void MyCustomLogFunction(const std::string& sLog)
{
  // Do custom logging things
}
<br />
VariableWatcher::WatchersManager::GetInstance().SetCustomLogFunction(MyCustomLogFunction);
</code>
</pre>
Please note that your function must have the same signature as the one given in example (returns void and take a const std::string& in parameter).
<br />
If you don't want the callstack of the changes in your logs, you can remove the PRINT_CALLSTACK symbol.

<h2>How does it work</h2>
This system allocate a whole memory page for your variable and adds a page guard protection on it. When anyone wants to access this memory area, it is notified by a custom handler which allows the system to log any new value stored in the variable.
