#include <mmdeviceapi.h>
#include <audioclient.h>
#include <Propsys.h>
#include <Functiondiscoverykeys_devpkey.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <clap/clap.h>
#include <clap/process.h>
#include "ClapHost.h"

//#include "SimpleClapHost.hh"

#define PLUGIN_PATH "moss-clap.clap"

bool load_clap_plugin(const char* pluginPath);

void process_audio_data(BYTE* pCaptureData, BYTE* pRenderData, UINT32 numFrames, WAVEFORMATEX* pwfx, ClapHostBuffer* input, ClapHostBuffer* output);

#define BUFFER_SIZE 9600

#define REFTIME_PER_MILLICEC (10) 

extern clap_plugin* plugin;

#pragma comment(lib, "Propsys.lib")

uint32_t event_size_zero(const struct clap_input_events* list) {
	UNREFERENCED_PARAMETER(list);
	return 0;
}

// Show device information
void PrintDeviceInfo(IMMDevice* pDevice) {
    if (pDevice == nullptr) {
        std::cerr << "Device is null" << std::endl;
        return;
    }

    IPropertyStore* pProps = nullptr;
    HRESULT hr = pDevice->OpenPropertyStore(STGM_READ, &pProps);
    if (FAILED(hr)) {
        std::cerr << "Failed to open property store. Error code: " << hr << std::endl;
        return;
    }

    PROPVARIANT varName;
    PropVariantInit(&varName);
    hr = pProps->GetValue(PKEY_Device_FriendlyName, &varName);
    if (FAILED(hr)) {
        std::cerr << "Failed to get device friendly name. Error code: " << hr << std::endl;
        return;
    }

    std::wcout << L"Device Name: " << varName.pwszVal << std::endl;

    PropVariantClear(&varName);
    pProps->Release();
}

// Process audio stream
void HandleAudioStream(IAudioClient* pAudioClientIn,
    IAudioClient* pAudioClientOut,
    IAudioCaptureClient* pCaptureClient,
    IAudioRenderClient* pRenderClient,
    WAVEFORMATEX* pwfx,
    ClapHostBuffer *input,
    ClapHostBuffer *output) {
    BYTE* pData;
    DWORD flags;
    UINT32 packetLength = 0;
    std::vector<BYTE> buffer(BUFFER_SIZE);

    pAudioClientIn->Start();
    pAudioClientOut->Start();

    if (pCaptureClient == nullptr) {
        std::cerr << "Capture client is null." << std::endl;
        return;
    }

    while (true) {
        pCaptureClient->GetNextPacketSize(&packetLength);
        if (packetLength == 0) {
            // If no data is available, sleep for a while
            std::this_thread::sleep_for(std::chrono::milliseconds(REFTIME_PER_MILLICEC));
            // Debug note
            std::wcout << L"Sleep 10 for Capture: " << std::endl;
            continue;
        }

        UINT32 numFramesAvailable;
        pCaptureClient->GetBuffer(&pData, &numFramesAvailable, &flags, nullptr, nullptr);

        // Copy data to buffer
        memcpy(buffer.data(), pData, numFramesAvailable * pwfx->nBlockAlign);
        pCaptureClient->ReleaseBuffer(numFramesAvailable);

        while (true) {
            HRESULT hr;
            UINT32 bufferFrameCount = 0;
            BYTE* pRenderData = nullptr;

            hr = pAudioClientOut->GetBufferSize(&bufferFrameCount);
            if (FAILED(hr)) {
                std::cerr << "Failed to get buffer size. Error code: " << hr << std::endl;
                return;
            }

            std::wcout << L"numFramesAvailable: " << numFramesAvailable << std::endl;

            hr = pRenderClient->GetBuffer(numFramesAvailable, &pRenderData);
            if (FAILED(hr)) {
                std::cerr << "Failed to get render buffer. Error code: " << hr << std::endl;
                return;
            }
            if (pRenderData == nullptr) {
                std::cerr << "Render data buffer is null." << std::endl;
                return;
            }
            // Write data to render buffer
            //memcpy(pRenderData, buffer.data(), numFramesAvailable * pwfx->nBlockAlign);

            process_audio_data(buffer.data(), pRenderData, numFramesAvailable, pwfx, input, output);

            hr = pRenderClient->ReleaseBuffer(numFramesAvailable, 0);
            if (FAILED(hr)) {
                std::cerr << "Failed to release render buffer. Error code: " << hr << std::endl;
                return;
            }
            break;
        }
    }

    pAudioClientIn->Stop();
    pAudioClientOut->Stop();
}

HRESULT StartAudioProcessing() {
    IMMDeviceEnumerator* pEnumerator = nullptr;
    IMMDevice* pDeviceIn = nullptr;
    IMMDevice* pDeviceOut = nullptr;
    IAudioClient* pAudioClientIn = nullptr;
    IAudioClient* pAudioClientOut = nullptr;
    IAudioCaptureClient* pCaptureClient = nullptr;
    IAudioRenderClient* pRenderClient = nullptr;

    HRESULT hr = CoInitialize(nullptr);
    if (FAILED(hr)) {
        std::cerr << "Failed to initialize COM library. Error code = 0x"
            << std::hex << hr << std::endl;
        return hr;
    }

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&pEnumerator));
    if (FAILED(hr)) {
        std::cerr << "Failed to create IMMDeviceEnumerator. Error code: " << hr << std::endl;
        return hr;
    }

    // 入力デバイスの取得
    hr = pEnumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &pDeviceIn);
    if (FAILED(hr)) {
        std::cerr << "Failed to get default audio capture device. Error code: " << hr << std::endl;
        return hr;
    }

    PrintDeviceInfo(pDeviceIn);

    // Get input audio client
    hr = pDeviceIn->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&pAudioClientIn);
    if (FAILED(hr)) {
        std::cerr << "Failed to activate input audio client. Error code: " << hr << std::endl;
        return hr;
    }

    // Get output device
    hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDeviceOut);
    if (FAILED(hr)) {
        std::cerr << "Failed to get default audio render device. Error code: " << hr << std::endl;
        return hr;
    }

    PrintDeviceInfo(pDeviceOut);

    // Get output audio client
    hr = pDeviceOut->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&pAudioClientOut);
    if (FAILED(hr)) {
        std::cerr << "Failed to activate output audio client. Error code: " << hr << std::endl;
        return hr;
    }

    // Get mix format
    WAVEFORMATEX* pwfx = nullptr;
    hr = pAudioClientIn->GetMixFormat(&pwfx);
    if (FAILED(hr)) {
        std::cerr << "Failed to get input mix format. Error code: " << hr << std::endl;
        return hr;
    }

    // Check if input format is supported
    WAVEFORMATEX* closestMatch = nullptr;
    hr = pAudioClientIn->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, pwfx, &closestMatch);
    if (hr == S_FALSE) {
        pwfx = closestMatch;
        std::cerr << "Input format not supported exactly, using closest match." << std::endl;
    }
    else if (FAILED(hr)) {
        std::cerr << "Input format not supported. Error code: " << hr << std::endl;
        return hr;
    }

    // Initialize audio clients
    hr = pAudioClientIn->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 10000000, 0, pwfx, nullptr);
    if (FAILED(hr)) {
        std::cerr << "Failed to initialize input audio client. Error code: " << hr << std::endl;
        return hr;
    }

    hr = pAudioClientOut->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 10000000, 0, pwfx, nullptr);
    if (FAILED(hr)) {
        std::cerr << "Failed to initialize output audio client. Error code: " << hr << std::endl;
        return hr;
    }

    hr = pAudioClientIn->GetService(IID_PPV_ARGS(&pCaptureClient));
    if (FAILED(hr)) {
        std::cerr << "Failed to get input capture client. Error code: " << hr << std::endl;
        return hr;
    }

    hr = pAudioClientOut->GetService(IID_PPV_ARGS(&pRenderClient));
    if (FAILED(hr)) {
        std::cerr << "Failed to get output render client. Error code: " << hr << std::endl;
        return hr;
    }

    // Show audio format
    std::wcout << L"Channels: " << pwfx->nChannels << std::endl;
    std::wcout << L"Sample Rate: " << pwfx->nSamplesPerSec << std::endl;
    std::wcout << L"Bits Per Sample: " << pwfx->wBitsPerSample << std::endl;

    ClapHostBuffer* input = new ClapHostBuffer();
    ClapHostBuffer* output = new ClapHostBuffer();

    HandleAudioStream(pAudioClientIn, pAudioClientOut, pCaptureClient, pRenderClient, pwfx, input, output);

    delete input;
    delete output;

    // Free resources
    pCaptureClient->Release();
    pRenderClient->Release();
    pAudioClientIn->Release();
    pAudioClientOut->Release();
    pDeviceIn->Release();
    pDeviceOut->Release();
    pEnumerator->Release();
    CoUninitialize();

    return S_OK;
}

// CLAPプラグインの運用
void process_audio_data(BYTE* pCaptureData, BYTE* pRenderData, UINT32 numFrames, WAVEFORMATEX* pwfx, ClapHostBuffer* input, ClapHostBuffer* output) {
    // CLAPバッファの準備
    clap_process process_data = {};
    process_data.frames_count = numFrames;

#if 0
    typedef struct clap_audio_buffer {
        // Either data32 or data64 pointer will be set.
        float** data32;
        double** data64;
        uint32_t channel_count;
        uint32_t latency; // latency from/to the audio interface
        uint64_t constant_mask;
    } clap_audio_buffer_t;
#endif

    // 入力と出力のオーディオバッファをCLAP形式に変換
    clap_audio_buffer input_buffer[1] = {0};
    clap_audio_buffer output_buffer[1] = {0};

#if 0

    //clap_audio_buffer_t input_bufferT[1] = { 0 };
    //clap_audio_buffer_t output_bufferT[1] = { 0 };


	if (pRenderData == 0) {
		std::cerr << "Buffer frame count is zero." << std::endl;
		return;
	}
	else if (numFrames > BUFFER_SIZE) {
		// Just for debug
		std::cerr << "numFrames is more than BUFFER_SIZE: " << numFrames << std::endl;
	}
#endif

#if 0 // debug
        printf("%u: %d,%d:%d,%d => %d,%d\n", i, i * 2, i * 2 + 1,
            pCaptureData[i * 2],
            pCaptureData[i * 2 + 1],
            pReorderedData[0][i],
            pReorderedData[1][i],
        );
#endif
	input_buffer[0].channel_count = 2;
	output_buffer[0].channel_count = 2;

    for (UINT i = 0; i < BUFFER_SIZE / 2; i++) {
        input->pReorderedBuffer[0][i] = pCaptureData[i * 2];
        input->pReorderedBuffer[1][i] = pCaptureData[i * 2 + 1];
    }



	//input_buffer[0].data32 = reinterpret_cast<float**>(&pCaptureData);
	//output_buffer[0].data32 = reinterpret_cast<float**>(&pRenderData);
	input_buffer[0].data32 = (float**)input->pReorderedBuffer;
    output_buffer[0].data32 = (float**) output->pReorderedBuffer;

    process_data.audio_inputs = &input_buffer[0];
    process_data.audio_outputs = &output_buffer[0];
    process_data.audio_inputs_count = 1;
    process_data.audio_outputs_count = 1;

    static clap_input_events_t inEvt ={ nullptr, event_size_zero, nullptr };
	process_data.in_events = &inEvt;
    process_data.out_events = nullptr;

    // CLAPプラグインのprocess関数を呼び出す
	plugin->process(plugin, &process_data); // プラグインの処理を実行

    // Now reordering the render buffer
	for (UINT i = 0; i < BUFFER_SIZE / 2; i++) {
		pRenderData[i * 2] = output->pReorderedBuffer[0][i];
		pRenderData[i * 2 + 1] = output->pReorderedBuffer[1][i];
	}
}

// Entry point
int main() {
    setlocale(LC_ALL, "Japanese");

    std::cout << "Sound Play!\n";

	if (!load_clap_plugin(PLUGIN_PATH)) {
		std::cerr << "Failed to load CLAP plugin." << std::endl;
		return -1;
	}

    std::wcout << L"Starting audio processing..." << std::endl;

    StartAudioProcessing();

    return 0;
}
