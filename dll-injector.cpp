#include <Windows.h>
#include <thread>
#include <iostream>
#include <TlHelp32.h>
#include <filesystem>

/*[TEMP] dll & process names*/
#define DLL_NAME "your_link_library.dll"
#define PROCESS_NAME L"your_process.exe"

/**
	gets info of a given process
	@param wchar_t* name
	@return uint32_t
*/
uint32_t get_process_info(const wchar_t* name)
{
	//get snapshot of current processes
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

	//if the snapshot is invalid, return false
	if (snapshot == INVALID_HANDLE_VALUE) { return false; }

	//create new process entry
	PROCESSENTRY32 entry = PROCESSENTRY32{ sizeof(PROCESSENTRY32) };

	//compare process name to wanted process name, if its the same, return the id, if its not, close the process handle and continue
	if (Process32First(snapshot, &entry)) { do { if (!wcscmp(entry.szExeFile, name)) { CloseHandle(snapshot); return entry.th32ProcessID; } } while (Process32Next(snapshot, &entry)); }

	//close process handle
	CloseHandle(snapshot);

	//return error
	return 0;
}

/**
	outputs message to console (04 = red/error; 10 = green/success; 15 = white/info)
	@param message string, color integer
	@return void
*/
void output_message(std::string msg, int color) {
	//get console handle
	HANDLE console_handle = GetStdHandle(STD_OUTPUT_HANDLE);

	//set console color to specified color in param
	SetConsoleTextAttribute(console_handle, color);

	//output message from param
	std::cout << msg << std::endl;

	//set console color back to white
	SetConsoleTextAttribute(console_handle, 15);
}

/**
	main function of the loader
	@param void
	@return status bool
*/
BOOL main()
{
	//create handles for processes and threads
	HANDLE process_handle;
	LPVOID path_alloc;
	HANDLE remote_thread;

	//lambda function for cleaning up memory
	auto cleanup = [&]() -> void {
		if (path_alloc) { VirtualFreeEx(process_handle, path_alloc, 0, MEM_RELEASE); }

		if (remote_thread) { CloseHandle(remote_thread); }

		if (process_handle) { CloseHandle(process_handle); }
	};

	//attempt to inject the dll into the process
	try
	{
		//get actual file path
		std::wstring file_path = std::filesystem::canonical(DLL_NAME).wstring();

		//check if file exists, throw error if it fails
		if (!std::filesystem::exists(file_path)) { throw std::string("file not found"); }

		//get process id of the process, throw error if it fails (process not open)
		uint32_t process_id = get_process_info(PROCESS_NAME);
		if (!process_id) { throw std::string("process not found"); }

		//get process handle
		process_handle = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_CREATE_THREAD, FALSE, process_id);

		//check if the process handle is open, throw error if it fails
		if (!process_handle) { throw std::string("cant open process"); }

		//allocate memory, throw error if it fails
		path_alloc = VirtualAllocEx(process_handle, NULL, file_path.size() * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
		if (!path_alloc) { throw std::string("couldnt allocate memory"); }

		//attempt to write the dll to the process memory, throw error if it fails
		if (!WriteProcessMemory(process_handle, path_alloc, file_path.c_str(), file_path.size() * 2, nullptr)) { throw std::string("failed to write dll to memory"); }

		//create remote thread (run dll)
		remote_thread = CreateRemoteThread(process_handle, NULL, NULL, reinterpret_cast<LPTHREAD_START_ROUTINE>(LoadLibraryW), path_alloc, NULL, NULL);

		//check if the thread was successful, throw error if it failed
		if (!remote_thread) { throw std::string("failed to create remote thread"); }

		//wait for thread to finish (inject)
		WaitForSingleObject(remote_thread, INFINITE);

		//injection was successful
		output_message("[SUCCESS] injected dll", 10);
	} //something went wrong, catch the error and output it to the console
	catch (const std::string & err) { output_message("[ERROR] " + err, 04); }

	//free memory
	cleanup();

	//output exit message
	output_message("[INFO] exiting in 10 seconds", 15);

	//sleep for 10 seconds
	std::this_thread::sleep_for(std::chrono::seconds(10));

	//exit the process
	return EXIT_SUCCESS;
}
