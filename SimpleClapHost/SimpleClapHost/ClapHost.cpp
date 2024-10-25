#include <clap/clap.h>
#include <Windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <vector>
#include <iostream>

#define BUFFER_SIZE 19200  // Process 10ms audio data

void process_audio_data(BYTE* pCaptureData, BYTE* pRenderData, UINT32 numFrames, WAVEFORMATEX* pwfx);

clap_plugin* plugin = nullptr;

bool load_clap_plugin(const char* pluginPath) {
    const clap_plugin_factory* pluginFactory = nullptr;
    const clap_host host = {
        {4,1,0}, // clap_version
        nullptr, // reserved (host_data)
		"Clap Test Host", // name
		"Device Drivers", // vender
		"http://www.devdrv.co.jp/", // url
		"0.1", // product version
    };

    HMODULE hModule = LoadLibraryA(pluginPath);
    if (!hModule) {
        std::cerr << "Failed to load plugin: " << pluginPath << std::endl;
        return false;
    }
    const clap_plugin_entry* pluginEntry = reinterpret_cast<const clap_plugin_entry*>(
        GetProcAddress(hModule, "clap_entry"));

    if (!pluginEntry || !pluginEntry->init(pluginPath)) {
        std::cerr << "Failed to initialize CLAP plugin:" << pluginPath << std::endl;
        return false;
    }

    pluginFactory =
        static_cast<const clap_plugin_factory*>(pluginEntry->get_factory(CLAP_PLUGIN_FACTORY_ID));

    auto count = pluginFactory->get_plugin_count(pluginFactory);

    auto desc = pluginFactory->get_plugin_descriptor(pluginFactory, 0 /* pluginIndex */);
    if (!desc) {
        std::cerr << "no plugin descriptor" << std::endl;
        return false;
    }

    plugin = const_cast<clap_plugin*>(pluginFactory->create_plugin(pluginFactory, &host, desc->id));
    if (!plugin) {
        std::cerr << "could not create the plugin with id: " << desc->id << std::endl;
        return false;
    }

    if (!plugin || plugin->init(plugin)) {
        std::cerr << "Failed to create CLAP plugin instance." << std::endl;
        return false;
    }

    return true;
}