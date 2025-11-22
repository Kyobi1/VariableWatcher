// This code was written by Clement Florant and is under MIT License
//
// Copyright(c) 2025 Clement Florant
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files(the "Software"),
// to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute,
// sublicense, and /or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions :
//
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
// WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#ifndef _VARIABLE_WATCHER_
#define _VARIABLE_WATCHER_

#include <windows.h>
#include <cstdint>

#include <string>

#define USE_COUT_FOR_DEFAULT_LOG
#define PRINT_CALLSTACK

#ifdef USE_COUT_FOR_DEFAULT_LOG
#include <iostream>

namespace VariableWatcher
{
	static void DefaultLogFunction(const std::string& sLog)
	{
		std::cout << sLog << std::endl;
	}
}
#else
namespace VariableWatcher
{
	static void DefaultLogFunction(const std::string& sLog)
	{
		printf("%s\n", sLog.c_str());
	}
}
#endif

#ifdef PRINT_CALLSTACK
#include <vector>
#include <DbgHelp.h>
#pragma comment(lib, "DbgHelp.lib")
#endif

namespace VariableWatcher
{
	class WatchersManager;

	class WatcherInterface
	{
	protected:
		WatcherInterface(const std::string& sName) : m_sName(sName) {}

		virtual void Log() const = 0;
		virtual void LogChanged() const = 0;

		std::string m_sName;

		friend class WatchersManager;
	};

	class WatchersManager
	{
	private:
		WatchersManager() : m_pWatcherSlots(), m_pWatcherSlotsRawMemory()
		{
			SYSTEM_INFO oSystemInfo;
			GetSystemInfo(&oSystemInfo);
			m_uSystemPageSize = oSystemInfo.dwPageSize;

			AddVectoredExceptionHandler(1, ExceptionHandler);

#ifdef PRINT_CALLSTACK
			SymSetOptions(SYMOPT_DEFERRED_LOADS);
#endif
		}
		WatchersManager(const WatchersManager&) = delete;
		WatchersManager& operator=(const WatchersManager&) = delete;

	public:
		using LogFunction = void( * )( const std::string& sLog );

		static WatchersManager& GetInstance()
		{
			static WatchersManager s_oWatchersManager;

			return s_oWatchersManager;
		}
		~WatchersManager()
		{
			bool bRemainingWatcherFound = false;
			for(uint32_t i = 0; i < s_uNbWatchers; ++i)
			{
				if(m_pWatcherSlots[i] != nullptr)
				{
					if(!bRemainingWatcherFound)
					{
						Log("There should be no more living watchers when destroying WatchersManager\nWatchers remaining :");
						bRemainingWatcherFound = true;
					}

					LogWatcherInfo(i);

					RemoveWatchedVariable(m_pWatcherSlots[i]);
				}
			}
		}

		static LONG ExceptionHandler(_EXCEPTION_POINTERS* pExceptionInfo)
		{
			WatchersManager& oWatchersManager = WatchersManager::GetInstance();

			const DWORD uExceptionCode = pExceptionInfo->ExceptionRecord->ExceptionCode;

			if(uExceptionCode == EXCEPTION_GUARD_PAGE)
			{
				const ULONG_PTR uReadOrWrite = pExceptionInfo->ExceptionRecord->ExceptionInformation[0];
				const ULONG_PTR uPageFaultAddress = pExceptionInfo->ExceptionRecord->ExceptionInformation[1];

				const bool bIsWrite = uReadOrWrite == 1U;
				bool bIsOnPage = false;
				uint32_t uFoundIndex = s_uNbWatchers;
				for(uint32_t i = 0; i < s_uNbWatchers && !bIsOnPage; ++i)
				{
					bIsOnPage |= uPageFaultAddress >= reinterpret_cast<ULONG_PTR>(oWatchersManager.m_pWatcherSlotsRawMemory[i]) && uPageFaultAddress < reinterpret_cast<ULONG_PTR>(oWatchersManager.m_pWatcherSlotsRawMemory[i]) + oWatchersManager.m_uSystemPageSize;
					if(bIsOnPage)
						uFoundIndex = i;
				}

				oWatchersManager.m_bIsLastAccessWrite = bIsWrite;
				if(bIsWrite && bIsOnPage)
				{
					DWORD uOldProtectMode;
					VirtualProtect(oWatchersManager.m_pWatcherSlotsRawMemory[uFoundIndex], oWatchersManager.m_uSystemPageSize, PAGE_READWRITE, &uOldProtectMode);

					oWatchersManager.m_uLastUsedSlot = uFoundIndex;

					pExceptionInfo->ContextRecord->EFlags |= 0x100;

					return EXCEPTION_CONTINUE_EXECUTION;
				}
				if(!bIsWrite && bIsOnPage)
				{
					DWORD uOldProtectMode;
					VirtualProtect(oWatchersManager.m_pWatcherSlotsRawMemory[uFoundIndex], oWatchersManager.m_uSystemPageSize, PAGE_READWRITE, &uOldProtectMode);

					oWatchersManager.m_uLastUsedSlot = uFoundIndex;

					pExceptionInfo->ContextRecord->EFlags |= 0x100;

					return EXCEPTION_CONTINUE_EXECUTION;
				}
				return EXCEPTION_CONTINUE_SEARCH;
			}
			else if(uExceptionCode == EXCEPTION_SINGLE_STEP)
			{
				pExceptionInfo->ContextRecord->EFlags &= ~0x100;

				if(oWatchersManager.m_bIsLastAccessWrite)
				{
					oWatchersManager.m_pWatcherSlots[oWatchersManager.m_uLastUsedSlot]->LogChanged();
				}

				DWORD uOldProtectMode;
				VirtualProtect(oWatchersManager.m_pWatcherSlotsRawMemory[oWatchersManager.m_uLastUsedSlot], oWatchersManager.m_uSystemPageSize, PAGE_READWRITE | PAGE_GUARD, &uOldProtectMode);

				oWatchersManager.m_uLastUsedSlot = s_uNbWatchers;
				oWatchersManager.m_bIsLastAccessWrite = false;

				return EXCEPTION_CONTINUE_EXECUTION;
			}

			return EXCEPTION_CONTINUE_SEARCH;
		}

		template<typename T>
		T* AddWatchedVariable(const WatcherInterface* pDebugVar)
		{
			if(sizeof(T) > m_uSystemPageSize)
			{
				Log(pDebugVar->m_sName + " is too big for system page sizes which is not handled right now");
				return nullptr;
			}

			uint32_t uFoundIndex = s_uNbWatchers;
			for(uint32_t i = 0; i < s_uNbWatchers; ++i)
			{
				if(m_pWatcherSlots[i] == nullptr)
				{
					uFoundIndex = i;
					break;
				}
			}

			if(uFoundIndex == s_uNbWatchers)
			{
				Log("No more slots available for " + pDebugVar->m_sName);
				return nullptr;
			}

			m_pWatcherSlotsRawMemory[uFoundIndex] = VirtualAlloc(nullptr, m_uSystemPageSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE | PAGE_GUARD);
			if(m_pWatcherSlotsRawMemory[uFoundIndex] != nullptr)
			{
				m_pWatcherSlots[uFoundIndex] = pDebugVar;
				return new ( m_pWatcherSlotsRawMemory[uFoundIndex] ) T;
			}
			else
			{
				Log("Alloc of " + pDebugVar->m_sName + " failed");
				return nullptr;
			}
		}

		void RemoveWatchedVariable(const WatcherInterface* pDebugVar)
		{
			uint32_t uFoundIndex = s_uNbWatchers;
			for(uint32_t i = 0; i < s_uNbWatchers; ++i)
			{
				if(pDebugVar == m_pWatcherSlots[i])
				{
					uFoundIndex = i;
					break;
				}
			}
			VirtualFree(m_pWatcherSlotsRawMemory[uFoundIndex], 0, MEM_RELEASE);
			m_pWatcherSlots[uFoundIndex] = nullptr;
			m_pWatcherSlotsRawMemory[uFoundIndex] = nullptr;
		}

		void Log(const std::string& sLog)
		{
			if(m_pLogFunction != nullptr)
				m_pLogFunction(sLog);
			else
				DefaultLogFunction(sLog);
		}

#ifdef PRINT_CALLSTACK
		std::string GetCallstack(const uint8_t uNbStackToRemove = s_uNbStackForLogging)
		{
			std::string sCallstack;
			HANDLE pProcess = InitCurrentProcess();

			if(pProcess == nullptr)
				return sCallstack;

			CONTEXT oContext{};
			RtlCaptureContext(&oContext);

			STACKFRAME64 oFrame{};
			oFrame.AddrPC.Offset = oContext.Rip;
			oFrame.AddrPC.Mode = AddrModeFlat;
			oFrame.AddrFrame.Offset = oContext.Rsp;
			oFrame.AddrFrame.Mode = AddrModeFlat;
			oFrame.AddrStack.Offset = oContext.Rsp;
			oFrame.AddrStack.Mode = AddrModeFlat;

			HANDLE pCurrentThread = GetCurrentThread();

			std::vector<DWORD64> aAddresses;

			while(StackWalk64(IMAGE_FILE_MACHINE_AMD64, pProcess, pCurrentThread, &oFrame, &oContext, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
			{
				if(oFrame.AddrPC.Offset == 0U)
					break;

				aAddresses.push_back(oFrame.AddrPC.Offset);
			}

			for(uint32_t i = uNbStackToRemove; i < aAddresses.size(); ++i)
			{
				char pBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)]{};
				PSYMBOL_INFO pSymbol = reinterpret_cast<PSYMBOL_INFO>(pBuffer);

				pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
				pSymbol->MaxNameLen = MAX_SYM_NAME;

				DWORD64 uDisplacement64 = 0U;

				bool bWriteSymbol = SymFromAddr(pProcess, aAddresses[i], &uDisplacement64, pSymbol);

				DWORD uDisplacement = 0U;

				IMAGEHLP_LINE64 oLine{};
				oLine.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

				bool bWriteLine = SymGetLineFromAddr64(pProcess, aAddresses[i], &uDisplacement, &oLine);

				if(bWriteLine)
					sCallstack += std::string(oLine.FileName) + ':';
				if(bWriteSymbol)
					sCallstack += std::string(pSymbol->Name);
				if(bWriteLine)
					sCallstack += ' ' + std::to_string(oLine.LineNumber);

				sCallstack += '\n';
			}

			return sCallstack;
		}
		
#endif

		void SetCustomLogFunction(LogFunction pFunc) { m_pLogFunction = pFunc; }

		static constexpr uint32_t s_uNbWatchers = 8U;

	private:

#ifdef PRINT_CALLSTACK
		HANDLE InitCurrentProcess()
		{
			DWORD uProcessID = GetCurrentProcessId();
			size_t uFoundIndex = m_aProcess.size();
			bool bProcessStoredIsInvalidated = false;
			for(size_t i = 0; i < m_aProcess.size(); ++i)
			{
				if(uProcessID == m_aProcess[i].first)
				{
					// Checking if the handle is still valid because an other process may have take his ID if it's terminated
					if(WaitForSingleObject(m_aProcess[i].second, 0) == WAIT_OBJECT_0)
					{
						bProcessStoredIsInvalidated = true;
					}
					uFoundIndex = i;
					break;
				}
			}

			if(bProcessStoredIsInvalidated)
			{
				m_aProcess.erase(m_aProcess.begin() + uFoundIndex);
				uFoundIndex = m_aProcess.size();
			}

			HANDLE pProcess = nullptr;
			if(uFoundIndex < m_aProcess.size())
			{
				pProcess = m_aProcess[uFoundIndex].second;
			}
			else
			{
				HANDLE pCurrentProcess = GetCurrentProcess();

				if(!DuplicateHandle(pCurrentProcess, pCurrentProcess, pCurrentProcess, &pProcess, 0, FALSE, DUPLICATE_SAME_ACCESS))
				{
					Log("Unable to duplicate handle of current process");
					return nullptr;
				}

				if(!SymInitialize(pProcess, nullptr, TRUE))
				{
					Log("Unable to initialize symbols of current process");
					return nullptr;
				}

				m_aProcess.emplace_back(uProcessID, pProcess);
			}

			return pProcess;
		}
#endif

		void LogWatcherInfo(const uint32_t uWatcherIndex)
		{
			Log("Watcher " + std::to_string(uWatcherIndex));
			m_pWatcherSlots[uWatcherIndex]->Log();
		}

		const WatcherInterface* m_pWatcherSlots[s_uNbWatchers];
		void* m_pWatcherSlotsRawMemory[s_uNbWatchers];

#ifdef PRINT_CALLSTACK
		std::vector<std::pair<DWORD, HANDLE>> m_aProcess;

		static constexpr uint8_t s_uNbStackForLogging = 6U;
#endif

		LogFunction m_pLogFunction = DefaultLogFunction;

		SIZE_T m_uSystemPageSize = 0U;
		uint32_t m_uLastUsedSlot = s_uNbWatchers;
		bool m_bIsLastAccessWrite = false;
	};

	template<typename T>
	struct Watcher : public WatcherInterface
	{
		Watcher(const std::string& sName) : WatcherInterface(sName)
		{
			using std::to_string;
			m_pVal = WatchersManager::GetInstance().AddWatchedVariable<T>(this);

			if(m_pVal != nullptr)
			{
				WatchersManager::GetInstance().Log("Create watched var " + m_sName + " with value " + to_string(*m_pVal));
#ifdef PRINT_CALLSTACK
				WatchersManager::GetInstance().Log("Callstack : \n" + WatchersManager::GetInstance().GetCallstack(1U));
#endif
			}
			else
				throw std::bad_alloc();
		}
		Watcher(const std::string& sName, const T& oCreateVal) : WatcherInterface(sName)
		{
			using std::to_string;
			m_pVal = WatchersManager::GetInstance().AddWatchedVariable<T>(this);

			if(m_pVal != nullptr)
			{
				*m_pVal = oCreateVal;
				WatchersManager::GetInstance().Log("Create watched var " + m_sName + " with value " + to_string(*m_pVal));
#ifdef PRINT_CALLSTACK
				WatchersManager::GetInstance().Log("Callstack : \n" + WatchersManager::GetInstance().GetCallstack(1U));
#endif
			}
			else
				throw std::bad_alloc();
		}
		~Watcher()
		{
			WatchersManager::GetInstance().Log("Remove watched var " + m_sName);
#ifdef PRINT_CALLSTACK
			WatchersManager::GetInstance().Log("Callstack : \n" + WatchersManager::GetInstance().GetCallstack(1U));
#endif

			WatchersManager::GetInstance().RemoveWatchedVariable(this);
		}

		T& operator=(const T& oVal)
		{
			*m_pVal = oVal;
			return *m_pVal;
		}

		T* operator&()
		{
			return m_pVal;
		}

		const Watcher* GetAddress() const { return this; }

		operator T() { return *m_pVal; }
		operator T& ( ) { return *m_pVal; }
		operator T* ( ) { return m_pVal; }

	private:
		void Log() const override
		{
			using std::to_string;
			WatchersManager::GetInstance().Log("Var " + m_sName + " : " + to_string(*m_pVal));
		}
		void LogChanged() const override
		{
			using std::to_string;
			WatchersManager::GetInstance().Log("Var " + m_sName + " changed : " + to_string(*m_pVal));
#ifdef PRINT_CALLSTACK
			WatchersManager::GetInstance().Log("Callstack : \n" + WatchersManager::GetInstance().GetCallstack());
#endif
		}

		T* m_pVal;

		friend class WatchersManager;
	};
}

#endif