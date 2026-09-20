
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>
#include <string>

#include "windows.h"
#include "strsafe.h"

HWND CreateSimpleWindow(const char* title);
char* GetLastErrorAsString();

struct WaveDevice;

struct WaveDeviceVTable {
	void * omitted[4];
	int (__thiscall * initialize) (WaveDevice *, HWND, unsigned);
	void * omitted_2[38];
};

struct WaveDevice {
	WaveDeviceVTable * vtable;
	// other fields omitted
};

struct MidiDevice;

struct MidiDeviceVTable {
	void * omitted[4];
	int (__thiscall * initialize) (MidiDevice *, HWND, unsigned);
	void * omitted_2[38];
};

struct MidiDevice {
	MidiDeviceVTable * vtable;
	// other fields omitted
};

struct SoundCore;

struct SoundCoreVTable {
	void * omitted[4];
	int (__thiscall * load_file) (SoundCore *, char const *); // takes file path
	void * omitted_2[2];
	int (__thiscall * play) (SoundCore *);
	int (__thiscall * stop) (SoundCore *); // slot 8, decoded in the AMB Editor (see preview.c)
	void * omitted_3[7];
	void (__thiscall * set_volume) (SoundCore *, int); // volume is >= 0 and <= 127
	void * omitted_4[5];
	int (__thiscall * m22) (SoundCore *);
	void * omitted_5;
	int (__thiscall * m24) (SoundCore *);
	void * omitted_6[2];
	int (__thiscall * set_flags) (SoundCore *, unsigned);
	void * omitted_7[22];
	int (__thiscall * m50) (SoundCore *);
	void * omitted_8[26];
};

struct SoundCore {
	SoundCoreVTable * vtable;
	// many more fields omitted
};

enum sound_core_type {
	SCT_DETECT_FROM_FILE_EXT = 0, // Does not work
	SCT_WAV,
	SCT_MIDI,
	SCT_AIF,
	SCT_4,
	SCT_AMB,
	SCT_6,
	SCT_7,
	SCT_8
};

typedef int (__cdecl * InitSoundTimer) (int param_1, int param_2);
typedef int (__cdecl * CreateSound) (SoundCore ** out_sound_core, char const * file_path, int sound_core_type);
typedef int (__cdecl * DeleteSound) (SoundCore * sound_core);
typedef int (__cdecl * CreateWaveDevice) (WaveDevice ** out, unsigned param_2);
typedef int (__cdecl * CreateMidiDevice) (MidiDevice ** out, unsigned param_2);


//
// Logging. Every line is stamped with milliseconds since startup so that the period of a looping sound can be read straight off the log.
//

DWORD g_start_tick = 0;

void
log_event (char const * format, ...)
{
	char msg[1000];
	va_list args;
	va_start (args, format);
	vsnprintf (msg, (sizeof msg) - 1, format, args);
	va_end (args);

	DWORD elapsed = GetTickCount () - g_start_tick;
	printf ("[%3u.%03u] %s\n", elapsed / 1000, elapsed % 1000, msg);
	fflush (stdout);
}


//
// Command line options. See PrintUsage for what they mean.
//

struct Options {
	char const * file;
	int type;
	char const * dir;
	int stop_after_ms;  // Call SoundCore::stop this long after play. -1 means never stop.
	int stall_at_ms;    // Stop pumping window messages at this time. -1 means never stall.
	int stall_for_ms;
	int run_for_ms;
	bool pump_messages;
};

void
PrintUsage ()
{
	printf ("Usage: sound_test.exe [options]\n"
		"\n"
		"  --file <path>      Sound to play. Relative paths are resolved against the Conquests\n"
		"                     directory. Default: Art\\Units\\Trebuchet\\TrebuchetAttack.AMB\n"
		"  --type <t>         amb, wav, midi or aif. Default: guessed from the file extension.\n"
		"  --dir <path>       Conquests directory. Default: looked up in the registry, which\n"
		"                     usually fails under Proton, so you'll likely need to pass this.\n"
		"  --stop-after <ms>  Call SoundCore::stop this long after starting playback. Omit to\n"
		"                     never stop, which shows whether playback loops on its own.\n"
		"  --stall <ms>       Stop pumping window messages for this long, starting right after\n"
		"                     playback begins. Simulates the game blocking its message loop.\n"
		"  --no-pump          Never pump window messages at all.\n"
		"  --run-for <ms>     Total run time before exiting. Default: 30000.\n"
		"\n"
		"Suggested experiments (run each inside your Proton prefix, and on Windows if you can,\n"
		"so you have a control to compare against):\n"
		"  1. sound_test.exe --dir <conquests>\n"
		"       Does an AMB sound loop forever on its own? If it loops under Proton but plays\n"
		"       once on Windows, sound.dll's own end-of-sound handling is what's broken.\n"
		"  2. sound_test.exe --dir <conquests> --stop-after 3000\n"
		"       Does stop() actually silence it? If the sound keeps going after stop() returns,\n"
		"       no amount of stopping from inside C3X will help and the fix must be harsher.\n"
		"  3. sound_test.exe --dir <conquests> --stall 5000\n"
		"       Does blocking the message loop make it loop or click? sound.dll is handed an\n"
		"       HWND, so it likely drives buffer recycling off window messages. If stalling\n"
		"       reproduces the bug, the root cause is message-pump timing, not the stop call.\n");
}

// Returns false if the command line was bad.
bool
ParseOptions (Options * opts, int argc, char ** argv)
{
	opts->file = "Art\\Units\\Trebuchet\\TrebuchetAttack.AMB";
	opts->type = -1;
	opts->dir = NULL;
	opts->stop_after_ms = -1;
	opts->stall_at_ms = -1;
	opts->stall_for_ms = 0;
	opts->run_for_ms = 30000;
	opts->pump_messages = true;

	for (int n = 1; n < argc; n++) {
		char const * arg = argv[n];
		char const * value = (n + 1 < argc) ? argv[n + 1] : NULL;

		if ((strcmp (arg, "--help") == 0) || (strcmp (arg, "-h") == 0))
			return false;

		else if (strcmp (arg, "--no-pump") == 0)
			opts->pump_messages = false;

		else if (value == NULL) {
			printf ("Option %s needs a value\n", arg);
			return false;

		} else if (strcmp (arg, "--file") == 0)
			opts->file = argv[++n];
		else if (strcmp (arg, "--dir") == 0)
			opts->dir = argv[++n];
		else if (strcmp (arg, "--stop-after") == 0)
			opts->stop_after_ms = atoi (argv[++n]);
		else if (strcmp (arg, "--run-for") == 0)
			opts->run_for_ms = atoi (argv[++n]);
		else if (strcmp (arg, "--stall") == 0) {
			opts->stall_at_ms = 0;
			opts->stall_for_ms = atoi (argv[++n]);

		} else if (strcmp (arg, "--type") == 0) {
			char const * t = argv[++n];
			if      (_stricmp (t, "amb" ) == 0) opts->type = SCT_AMB;
			else if (_stricmp (t, "wav" ) == 0) opts->type = SCT_WAV;
			else if (_stricmp (t, "midi") == 0) opts->type = SCT_MIDI;
			else if (_stricmp (t, "aif" ) == 0) opts->type = SCT_AIF;
			else {
				printf ("Unrecognized sound type: %s\n", t);
				return false;
			}

		} else {
			printf ("Unrecognized option: %s\n", arg);
			return false;
		}
	}

	// Guess the type from the file extension. Passing SCT_DETECT_FROM_FILE_EXT to create_sound does not work, so we have to do this ourselves.
	if (opts->type < 0) {
		char const * dot = strrchr (opts->file, '.');
		if      (dot == NULL)                 opts->type = SCT_AMB;
		else if (_stricmp (dot, ".amb") == 0) opts->type = SCT_AMB;
		else if (_stricmp (dot, ".wav") == 0) opts->type = SCT_WAV;
		else if (_stricmp (dot, ".mid") == 0) opts->type = SCT_MIDI;
		else if (_stricmp (dot, ".aif") == 0) opts->type = SCT_AIF;
		else {
			printf ("Can't guess sound type from file name \"%s\", pass --type\n", opts->file);
			return false;
		}
	}

	return true;
}

// Sets the current directory to the Conquests folder. Returns false on failure.
bool
GoToConquestsDirectory (char const * override_dir)
{
	if (override_dir != NULL) {
		if (! SetCurrentDirectory (override_dir)) {
			log_event ("Couldn't enter \"%s\": %s", override_dir, GetLastErrorAsString ());
			return false;
		}
		return true;
	}

	char civ_3_install_path[1000] = {0};
	DWORD buf_size = (sizeof civ_3_install_path) - 1;
	HKEY reg_key;
	if (RegOpenKeyExA (HKEY_LOCAL_MACHINE, "SOFTWARE\\Infogrames Interactive\\Civilization III", 0, KEY_READ, &reg_key) == ERROR_SUCCESS) {
		RegQueryValueExA (reg_key, "Install_Path", NULL, NULL, (LPBYTE)civ_3_install_path, &buf_size);
		RegCloseKey (reg_key);
	}
	if (civ_3_install_path[0] == '\0') {
		log_event ("Couldn't find the Civ 3 install in the registry. Pass --dir with the path to your Conquests folder.");
		return false;
	}

	char conquests_path[1000] = {0};
	snprintf (conquests_path, (sizeof conquests_path) - 1, "%s%s", civ_3_install_path, "Conquests");
	if (! SetCurrentDirectory (conquests_path)) {
		log_event ("Couldn't enter \"%s\": %s", conquests_path, GetLastErrorAsString ());
		return false;
	}
	return true;
}

int
main (int argc, char ** argv)
{
	int result;
	g_start_tick = GetTickCount ();

	Options opts;
	if (! ParseOptions (&opts, argc, argv)) {
		PrintUsage ();
		return 1;
	}

	if (! GoToConquestsDirectory (opts.dir))
		return 1;

	HMODULE sound_module = LoadLibraryA ("sound.dll");
	if (sound_module == NULL) {
		log_event ("Couldn't load sound.dll: %s", GetLastErrorAsString ());
		return 1;
	}

	InitSoundTimer   init_sound_timer   = reinterpret_cast<InitSoundTimer>  (GetProcAddress (sound_module, "init_sound_timer"));
	CreateSound      create_sound       = reinterpret_cast<CreateSound>     (GetProcAddress (sound_module, "create_sound"));
	DeleteSound      delete_sound       = reinterpret_cast<DeleteSound>     (GetProcAddress (sound_module, "delete_sound"));
	CreateWaveDevice create_wave_device = reinterpret_cast<CreateWaveDevice>(GetProcAddress (sound_module, (LPCSTR)5)); // Name of Dll_Wave_Device::create_device is mangled so use ordinal here
	CreateMidiDevice create_midi_device = reinterpret_cast<CreateMidiDevice>(GetProcAddress (sound_module, (LPCSTR)7));

	if ((init_sound_timer == NULL) || (create_sound == NULL) || (delete_sound == NULL) ||
	    (create_wave_device == NULL) || (create_midi_device == NULL)) {
		log_event ("Couldn't find the expected exports in sound.dll. Is this really a Civ 3 sound.dll?");
		return 1;
	}

	HWND window = CreateSimpleWindow ("Sound Test");

	init_sound_timer (0, 0); // don't know what these params are for, but Civ 3 passes zero for both

	WaveDevice * wave_device;
	create_wave_device (&wave_device, 0); // second parameter is not used
	wave_device->vtable->initialize (wave_device, window, 2); // pass 2 for flags. I think that's what Civ 3 uses.

	MidiDevice * midi_device;
	create_midi_device (&midi_device, 0);
	midi_device->vtable->initialize (midi_device, window, 0);

	log_event ("Playing \"%s\" as type %d", opts.file, opts.type);

	SoundCore * core;
	result = create_sound (&core, opts.file, opts.type);
	log_event ("create_sound returned %d (expected 0)", result);

	result = core->vtable->m22 (core);
	log_event ("m22 returned %d (expected 0)", result);

	result = core->vtable->set_flags (core, 0); // Civ 3 passes 0 for flags here
	log_event ("set_flags returned %d (expected 0)", result);

	result = core->vtable->m24 (core);
	log_event ("m24 returned %d (expected 0)", result);

	result = core->vtable->load_file (core, opts.file);
	log_event ("load_file returned %d (expected 0)", result);

	// I observed this function returning 1496 when running inside Civ 3. It does not return the same thing when run here. I don't what the return
	// value means. The function is a "getter" and does no actual work.
	// result = core->vtable->m50 (core);
	// log_event ("m50 returned %d (don't know what to expect)", result);

	core->vtable->set_volume (core, 127);
	result = core->vtable->play (core);
	log_event ("play returned %d (expected 0)", result);

	if (opts.stop_after_ms >= 0)
		log_event ("Will call stop in %d ms. LISTEN: does the sound actually go silent then?", opts.stop_after_ms);
	else
		log_event ("Will never call stop. LISTEN: does the sound play once and end, or loop forever?");
	if (opts.stall_for_ms > 0)
		log_event ("Will stop pumping window messages for %d ms, starting now.", opts.stall_for_ms);

	// Main loop. We can't just block in GetMessage because we need to act at specific times, so pump with PeekMessage instead and sleep a
	// little each pass to avoid spinning a core at 100%.
	DWORD loop_start = GetTickCount ();
	bool stop_done = false, stall_done = (opts.stall_for_ms <= 0);
	while (true) {
		DWORD elapsed = GetTickCount () - loop_start;

		if (elapsed >= (DWORD)opts.run_for_ms)
			break;

		if ((! stall_done) && (elapsed >= (DWORD)opts.stall_at_ms)) {
			// Deliberately block without pumping messages. If sound.dll drives buffer recycling off window messages, this is where a
			// Wine-specific fault should become audible as a stutter, a click, or a sound that never ends.
			log_event ("Stalling the message loop for %d ms...", opts.stall_for_ms);
			Sleep (opts.stall_for_ms);
			log_event ("...done stalling. Did you hear clicking or a stuck loop?");
			stall_done = true;
			continue;
		}

		if ((! stop_done) && (opts.stop_after_ms >= 0) && (elapsed >= (DWORD)opts.stop_after_ms)) {
			result = core->vtable->stop (core);
			log_event ("stop returned %d. LISTEN NOW: is it really silent?", result);
			stop_done = true;
		}

		if (opts.pump_messages) {
			MSG msg;
			while (PeekMessage (&msg, NULL, 0, 0, PM_REMOVE)) {
				if (msg.message == WM_QUIT)
					goto done;
				TranslateMessage (&msg);
				DispatchMessage (&msg);
			}
		}

		Sleep (5);
	}
done:

	log_event ("Run finished. Cleaning up.");
	if (! stop_done)
		core->vtable->stop (core);
	delete_sound (core);

	// Need to call TerminateProcess to close the program otherwise sound.dll will keep it alive in the background.
	TerminateProcess (GetCurrentProcess (), 0);
	return 0; // Unreachable
}



//
// Helper functions (all written by ChatGPT):
//

// Function prototype for the Window Procedure
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

// Function to create and show a simple window
HWND CreateSimpleWindow(const char* title) {
    // Register the window class
    const char CLASS_NAME[] = "Simple Window Class";
    WNDCLASS wc = {};

    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = CLASS_NAME;

    RegisterClass(&wc);

    // Create the window
    HWND hwnd = CreateWindowEx(
        0,                              // Optional window styles
        CLASS_NAME,                     // Window class
        title,                          // Window text
        WS_OVERLAPPEDWINDOW,            // Window style

        // Size and position
        CW_USEDEFAULT, CW_USEDEFAULT, 200, 100,

        NULL,       // Parent window
        NULL,       // Menu
        wc.hInstance,  // Instance handle
        NULL        // Additional application data
    );

    ShowWindow(hwnd, SW_SHOW);

    return hwnd;
}

// Window procedure function
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

char* GetLastErrorAsString() {
    // Retrieve the last error code
    DWORD errorMessageID = ::GetLastError();
    if(errorMessageID == 0) {
        // No error message has been recorded
        return strdup("No error");
    }

    LPSTR messageBuffer = nullptr;
    size_t size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        errorMessageID,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&messageBuffer,
        0, nullptr);

    // Copy the error message into a std::string
    std::string message(messageBuffer, size);

    // Free the buffer allocated by the system
    LocalFree(messageBuffer);

    // Return a copy of the string as a char*
    return strdup(message.c_str());
}
