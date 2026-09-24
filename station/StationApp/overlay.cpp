#include "overlay.h"
#include "logging.h"
#include <thread>   // For std::this_thread::sleep_for
#include <chrono>   // For std::chrono::milliseconds
#include <algorithm> // For std::max
#include <mutex>    // For std::mutex
#include "steam_games.h"
#include <future>
#include "steam_api.h"

// DirectX / Direct2D Headers
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h> // For ComPtr
#include <wincodec.h> // For WIC
#pragma comment(lib, "windowscodecs.lib")
#include "network.h" // For HttpDownloadToVector
#include <vector>
#include <urlmon.h> // For URL parsing helper (optional but good)
#pragma comment(lib, "urlmon.lib")
#include <memory> // For std::unique_ptr
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

// Custom message definitions
#define WM_APP_GAME_DATA_READY (WM_APP + 100)

// Linker Dependencies (ensure these are in project settings)
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

using Microsoft::WRL::ComPtr; // Use ComPtr for simplicity

// <ai_context>
// Implementation of VR overlay logic using DirectX
// </ai_context>

// Global Constants (moved from previous GDI impl)
const int OVERLAY_WIDTH = 1024;
const int OVERLAY_HEIGHT = 512;
// #define OVERLAY_REFRESH_INTERVAL 16  // Already defined in globals.h

// Global variables for overlay UI state


// Define the error code if it's not already defined in the headers
#ifndef VROverlayError_AlreadyVisible
#define VROverlayError_AlreadyVisible 12
#endif

// Forward declarations to fix function order issues
void CleanupVideo();
bool ResetVideoReader();
void VideoProcessingThread();

// --- DirectX Initialization/Cleanup ---

// Helper macro for safe COM release
#define SAFE_RELEASE(p) { if(p) { (p)->Release(); (p)=nullptr; } }

void CleanupOverlayDirectX() {
    SAFE_RELEASE(g_pTextFormatCategoryList); // Release VR category list text format
    Log("CleanupOverlayDirectX: Starting cleanup...");
    Log("Cleaning up DirectX resources for overlay...");
    
    // First clean up video resources
    CleanupVideo();
    
    SAFE_RELEASE(g_pUpArrowBitmap);
    SAFE_RELEASE(g_pDownArrowBitmap);
    
    SAFE_RELEASE(g_pDescTextLayout); // Add this line to release the description text layout
    g_currentLayoutAppId = "";       // Reset the current layout app ID
    
    SAFE_RELEASE(g_pTextFormatTime);
    SAFE_RELEASE(g_pTextFormatStatus);
    SAFE_RELEASE(g_pTextFormatSessionTime);
    SAFE_RELEASE(g_pTextFormatQuitButton);
    SAFE_RELEASE(g_pDWriteFactory);
    SAFE_RELEASE(g_pWhiteBrush);
    SAFE_RELEASE(g_pD2DTargetBitmap);
    SAFE_RELEASE(g_pD2DContext);
    SAFE_RELEASE(g_pD2DDevice);
    SAFE_RELEASE(g_pD2DFactory);
    SAFE_RELEASE(g_pDXGISurface);
    SAFE_RELEASE(g_pOverlayRenderTargetView); // If used
    SAFE_RELEASE(g_pOverlayTexture);
    SAFE_RELEASE(g_pImmediateContext);
    // Add swap chain release if it were used
    SAFE_RELEASE(g_pD3DDevice);
    SAFE_RELEASE(pBlackBrush);
    SAFE_RELEASE(g_pArrowBrush);
    Log("DirectX resources cleaned up.");
    Log("CleanupOverlayDirectX: Finished cleanup.");
}

// --- Video Initialization/Cleanup ---

void CleanupVideo()
{
    Log("Cleaning up video resources...");
    
    // Signal thread to stop
    g_stopVideoThread = true;
    
    // Don't wait for thread here - we may have already detached it
    // or handled the join in HideOverlayContinuous
    
    // Release the video bitmaps
    {
        std::lock_guard<std::mutex> lock(g_videoFrameMutex);
        SAFE_RELEASE(g_pVideoFrameBitmap);
        // SAFE_RELEASE(g_nextFrameBitmap); // Remove if not needed
        g_rawVideoFrameBuffer.clear();
        g_rawVideoFrameBuffer.shrink_to_fit();
        g_rawVideoFrameWidth = 0;
        g_rawVideoFrameHeight = 0;
        g_rawVideoFrameReady = false;
    }
    
    // Then release the source reader
    SAFE_RELEASE(g_pSourceReader);
    
    // Make sure Media Foundation is shut down
    g_videoInitialized = false;
    // HRESULT hr = MFShutdown(); // <--- REMOVED: Let main.cpp handle MF lifecycle
    // if (FAILED(hr)) {
    //     Log("CleanupVideo -> Failed to shut down Media Foundation. HRESULT: 0x" + std::to_string(hr));
    // } else {
    //     Log("CleanupVideo -> Media Foundation successfully shut down");
    // }
    
    Log("Video resources cleaned up.");
}

bool InitializeVideo()
{
    Log("InitializeVideo -> Starting video initialization...");
    
    // // Make sure Media Foundation is started // <--- REMOVED: Let main.cpp handle MF lifecycle
    // HRESULT hr = MFStartup(MF_VERSION);
    // if (FAILED(hr))
    // {
    //     Log("InitializeVideo -> Failed to initialize Media Foundation. HRESULT: 0x" + std::to_string(hr));
    //     return false;
    // }
    
    // Construct video path: exe folder + media\logo-loop.mp4
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    PathRemoveFileSpecW(exePath);
    g_videoPath = std::wstring(exePath) + L"\\media\\logo-loop.mp4";
    
    Log("InitializeVideo -> Looking for video file: " + WStringToString(g_videoPath));
    
    // Check if file exists
    DWORD attrs = GetFileAttributesW(g_videoPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES)
    {
        Log("InitializeVideo -> ERROR: Video file not found! Path: " + WStringToString(g_videoPath));
        Log("InitializeVideo -> Please ensure the media folder exists and contains logo-loop.mp4");
        return false;
    }
    
    Log("InitializeVideo -> Video file found, creating source reader...");
    
    // Create attributes for low latency mode
    IMFAttributes* pAttributes = NULL;
    HRESULT hr = MFCreateAttributes(&pAttributes, 2);
    if (SUCCEEDED(hr))
    {
        // Set low-latency hint
        hr = pAttributes->SetUINT32(MF_LOW_LATENCY, TRUE);
        if (SUCCEEDED(hr))
        {
            Log("InitializeVideo -> Set low latency attribute");
        }
        
        // Create source reader with attributes
        hr = MFCreateSourceReaderFromURL(g_videoPath.c_str(), pAttributes, &g_pSourceReader);
        SAFE_RELEASE(pAttributes);
    }
    else
    {
        // Fall back to creating source reader without attributes
        hr = MFCreateSourceReaderFromURL(g_videoPath.c_str(), NULL, &g_pSourceReader);
    }
    
    if (FAILED(hr))
    {
        Log("InitializeVideo -> Failed to create Media Foundation source reader. HRESULT: 0x" + std::to_string(hr));
        return false;
    }
    
    Log("InitializeVideo -> Source reader created, examining native format...");
    
    // Try to get native format info first
    IMFMediaType* pNativeType = NULL;
    hr = g_pSourceReader->GetNativeMediaType(
        MF_SOURCE_READER_FIRST_VIDEO_STREAM,
        0,  // First available type
        &pNativeType
    );
    
    if (SUCCEEDED(hr) && pNativeType) {
        // Log native format info
        GUID majorType, subType;
        pNativeType->GetGUID(MF_MT_MAJOR_TYPE, &majorType);
        pNativeType->GetGUID(MF_MT_SUBTYPE, &subType);
        
        UINT32 width = 0, height = 0;
        MFGetAttributeSize(pNativeType, MF_MT_FRAME_SIZE, &width, &height);
        
        // Convert GUID to string for logging
        OLECHAR guidString[39];
        StringFromGUID2(subType, guidString, 39);
        std::wstring wSubtype(guidString);
        
        Log("InitializeVideo -> Native video format: " + WStringToString(wSubtype) + 
            ", Dimensions: " + std::to_string(width) + "x" + std::to_string(height));
            
        SAFE_RELEASE(pNativeType);
    }
    
    Log("InitializeVideo -> Configuring video format...");
    
    // Try multiple formats in order of preference
    const GUID formats[] = {
        MFVideoFormat_RGB32,    // First try RGB32 (our preferred format)
        MFVideoFormat_RGB24,    // Then RGB24
        MFVideoFormat_YUY2,     // Then YUY2 (more widely supported)
        MFVideoFormat_NV12      // Then NV12 (very common hardware format)
    };
    
    const char* formatNames[] = {
        "RGB32", "RGB24", "YUY2", "NV12"
    };
    
    bool formatConfigured = false;
    
    for (int i = 0; i < 4; i++) {
        Log("InitializeVideo -> Trying format: " + std::string(formatNames[i]));
        
        IMFMediaType* pMediaType = NULL;
        hr = MFCreateMediaType(&pMediaType);
        if (SUCCEEDED(hr)) {
            hr = pMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            if (SUCCEEDED(hr)) {
                hr = pMediaType->SetGUID(MF_MT_SUBTYPE, formats[i]);
                if (SUCCEEDED(hr)) {
                    hr = g_pSourceReader->SetCurrentMediaType(
                        (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 
                        NULL, 
                        pMediaType);
                    
                    if (SUCCEEDED(hr)) {
                        Log("InitializeVideo -> Successfully configured format: " + std::string(formatNames[i]));
                        formatConfigured = true;
                        SAFE_RELEASE(pMediaType);
                        break;
                    } else {
                        Log("InitializeVideo -> Failed to set format " + std::string(formatNames[i]) + 
                            ". HRESULT: 0x" + std::to_string(hr));
                    }
                }
            }
            SAFE_RELEASE(pMediaType);
        }
    }
    
    if (!formatConfigured) {
        Log("InitializeVideo -> Failed to configure any video format. Will use fallback background.");
        SAFE_RELEASE(g_pSourceReader);
        return false;
    }
    
    // We've already set the low latency mode via attributes
    // Request first sample to prime the pipeline
    Log("InitializeVideo -> Priming the video pipeline");
    DWORD streamIndex;
    DWORD flags;
    LONGLONG timestamp;
    IMFSample* pSample = NULL;
    
    hr = g_pSourceReader->ReadSample(
        MF_SOURCE_READER_FIRST_VIDEO_STREAM,
        0,
        &streamIndex,
        &flags,
        &timestamp,
        &pSample
    );
    
    if (SUCCEEDED(hr) && pSample) {
        Log("InitializeVideo -> Successfully primed the pipeline with first frame");
        SAFE_RELEASE(pSample);
    }
    
    // Get media type to log video dimensions and detect frame rate
    IMFMediaType* pOutputMediaType = NULL;
    UINT32 width = 0, height = 0;
    hr = g_pSourceReader->GetCurrentMediaType(
        MF_SOURCE_READER_FIRST_VIDEO_STREAM,
        &pOutputMediaType
    );
    if (SUCCEEDED(hr))
    {
        MFGetAttributeSize(pOutputMediaType, MF_MT_FRAME_SIZE, &width, &height);
        Log("InitializeVideo -> Output video dimensions: " + std::to_string(width) + "x" + std::to_string(height));
        
        // Try to get the frame rate from the media type
        UINT32 numerator = 0, denominator = 0;
        hr = MFGetAttributeRatio(pOutputMediaType, MF_MT_FRAME_RATE, &numerator, &denominator);
        if (SUCCEEDED(hr) && denominator > 0) {
            g_videoFrameRate = (float)numerator / (float)denominator;
            Log("InitializeVideo -> Detected video frame rate: " + std::to_string(g_videoFrameRate) + " FPS");
        } else {
            g_videoFrameRate = 30.0f; // Default fallback
            Log("InitializeVideo -> Could not detect frame rate, using default: " + std::to_string(g_videoFrameRate) + " FPS");
        }
        
        SAFE_RELEASE(pOutputMediaType);
    }
    
    Log("InitializeVideo -> Video initialization SUCCESSFUL!");
    g_videoInitialized = true;
    return true;
}



bool InitializeOverlayDirectX() {
    Log("InitializeOverlayDirectX: Function entered.");
    HRESULT hr;
    // ... (existing code above)


    // --- Define Layout Rectangles ---
    float overlayWidth = static_cast<float>(OVERLAY_WIDTH);
    float overlayHeight = static_cast<float>(OVERLAY_HEIGHT);
    float padding = 20.0f;
    float categoryListWidth = overlayWidth * 0.20f;
    float categoryScrollButtonWidth = 30.0f;
    float categoryScrollButtonHeight = 30.0f;
    float scrollButtonPadding = 5.0f;

    // Category List (Far Left Side)
    g_rectCategoryList = D2D1::RectF(
        padding,
        padding,
        padding + categoryListWidth,
        overlayHeight - padding
    );
    // Category List Scroll Up Button
    g_rectCatScrollUpButton = D2D1::RectF(
        g_rectCategoryList.right + scrollButtonPadding,
        g_rectCategoryList.top,
        g_rectCategoryList.right + scrollButtonPadding + categoryScrollButtonWidth,
        g_rectCategoryList.top + categoryScrollButtonHeight
    );
    // Category List Scroll Down Button
    g_rectCatScrollDownButton = D2D1::RectF(
        g_rectCategoryList.right + scrollButtonPadding,
        g_rectCategoryList.bottom - categoryScrollButtonHeight,
        g_rectCategoryList.right + scrollButtonPadding + categoryScrollButtonWidth,
        g_rectCategoryList.bottom
    );
    // Adjust Game List and Details Panel
    float gameListStartX = g_rectCatScrollDownButton.right + padding;
    float gameListWidth = overlayWidth * 0.25f;
    g_rectGameList = D2D1::RectF(
        gameListStartX,
        padding,
        gameListStartX + gameListWidth,
        overlayHeight - padding
    );
    float gameListScrollButtonWidth = 60.0f;  // Increased size
    float gameListScrollButtonHeight = 60.0f; // Increased size
    g_rectScrollUpButton = D2D1::RectF(
        g_rectGameList.right + scrollButtonPadding,
        g_rectGameList.top,
        g_rectGameList.right + scrollButtonPadding + gameListScrollButtonWidth,
        g_rectGameList.top + gameListScrollButtonHeight
    );
    g_rectScrollDownButton = D2D1::RectF(
        g_rectGameList.right + scrollButtonPadding,
        g_rectGameList.bottom - gameListScrollButtonHeight,
        g_rectGameList.right + scrollButtonPadding + gameListScrollButtonWidth,
        g_rectGameList.bottom
    );
    float detailsPanelContentStartX = g_rectScrollDownButton.right + padding;
    float detailsPanelOverallEndX = overlayWidth - padding;
    float descScrollButtonWidth = 30.0f;
    float descScrollButtonHeight = 30.0f;
    float descButtonPadding = 5.0f;
    float descScrollButtonSpace = descScrollButtonWidth + descButtonPadding;
    float headerHeight = 120.0f;
    float buttonHeight = 48.0f;
    float detailsContentAreaWidth = (detailsPanelOverallEndX - detailsPanelContentStartX) - descScrollButtonSpace;
    g_rectHeaderImage = D2D1::RectF(
        detailsPanelContentStartX,
        padding,
        detailsPanelContentStartX + detailsContentAreaWidth,
        padding + headerHeight
    );
    g_rectDescription = D2D1::RectF(
        detailsPanelContentStartX,
        g_rectHeaderImage.bottom + padding,
        detailsPanelContentStartX + detailsContentAreaWidth,
        overlayHeight - padding - buttonHeight - padding
    );
    // Define widths for Start and Quit buttons
    float startGameButtonWidth = detailsContentAreaWidth * 0.66f; // Approx 2/3
    float quitGameButtonWidth = detailsContentAreaWidth * 0.33f; // Approx 1/3
    float buttonSpacing = 5.0f; // Small space between buttons

    g_rectStartButton = D2D1::RectF(
        detailsPanelContentStartX,
        overlayHeight - padding - buttonHeight,
        detailsPanelContentStartX + startGameButtonWidth,
        overlayHeight - padding
    );

    g_rectQuitButton = D2D1::RectF(
        g_rectStartButton.right + buttonSpacing, // Position Quit button to the right of Start
        overlayHeight - padding - buttonHeight,
        g_rectStartButton.right + buttonSpacing + quitGameButtonWidth,
        overlayHeight - padding
    );
    g_rectDescScrollUpButton = D2D1::RectF(
        g_rectDescription.right + scrollButtonPadding,
        g_rectDescription.top,
        g_rectDescription.right + scrollButtonPadding + descScrollButtonWidth,
        g_rectDescription.top + descScrollButtonHeight
    );
    g_rectDescScrollDownButton = D2D1::RectF(
        g_rectDescription.right + scrollButtonPadding,
        g_rectDescription.bottom - descScrollButtonHeight,
        g_rectDescription.right + scrollButtonPadding + descScrollButtonWidth,
        g_rectDescription.bottom
    );
    Log("InitializeOverlayDirectX: Layout rectangles redefined for categories.");
    // ... (rest of function remains unchanged)

    // ... (rest of function remains unchanged)

    Log("InitializeOverlayDirectX: Starting initialization...");
    // ... (existing code)

    // --- Create D3D11 Device and Context ---
    UINT createDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT; // Needed for D2D
#ifdef _DEBUG
    //createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        0,
        createDeviceFlags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &device,
        nullptr,
        &context
    );
    if (FAILED(hr)) {
        Log("InitializeOverlayDirectX: Failed to create D3D11 device. HRESULT: " + std::to_string(hr));
        return false;
    }
    g_pD3DDevice = device.Detach();
    g_pImmediateContext = context.Detach();
    Log("InitializeOverlayDirectX: D3D11 Device and Context created.");

    // --- Create D2D/DWrite Factories ---
    D2D1_FACTORY_OPTIONS options;
    ZeroMemory(&options, sizeof(D2D1_FACTORY_OPTIONS));
#ifdef _DEBUG
    options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, options, &g_pD2DFactory);
    if (FAILED(hr)) { Log("InitializeOverlayDirectX: Failed to create D2D factory. HRESULT: " + std::to_string(hr)); CleanupOverlayDirectX(); return false; }
    Log("InitializeOverlayDirectX: D2D Factory created.");

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&g_pDWriteFactory));
    if (FAILED(hr)) { Log("InitializeOverlayDirectX: Failed to create DWrite factory. HRESULT: " + std::to_string(hr)); CleanupOverlayDirectX(); return false; }
    Log("InitializeOverlayDirectX: DWrite Factory created.");
    Log("InitializeOverlayDirectX: D2D/DWrite Factories creation attempted.");

    // --- Create D2D Device/Context ---
    ComPtr<IDXGIDevice> dxgiDevice;
    hr = g_pD3DDevice->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(dxgiDevice.GetAddressOf()));
    if (FAILED(hr)) { Log("InitializeOverlayDirectX: Failed to get IDXGIDevice from D3D device. HRESULT: " + std::to_string(hr)); CleanupOverlayDirectX(); return false; }
    Log("InitializeOverlayDirectX: QI for DXGI Device succeeded.");
    Log("InitializeOverlayDirectX: D2D Device/Context creation attempted.");

    hr = g_pD2DFactory->CreateDevice(dxgiDevice.Get(), &g_pD2DDevice);
    if (FAILED(hr)) { Log("InitializeOverlayDirectX: Failed to create D2D device. HRESULT: " + std::to_string(hr)); CleanupOverlayDirectX(); return false; }
    Log("InitializeOverlayDirectX: D2D Device (g_pD2DDevice) created.");

    hr = g_pD2DDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &g_pD2DContext);
    if (FAILED(hr)) { Log("InitializeOverlayDirectX: Failed to create D2D device context. HRESULT: " + std::to_string(hr)); CleanupOverlayDirectX(); return false; }
    Log("InitializeOverlayDirectX: D2D Context (g_pD2DContext) created.");

    // --- Create Overlay Texture (D3D11) ---
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = OVERLAY_WIDTH;
    desc.Height = OVERLAY_HEIGHT;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    hr = g_pD3DDevice->CreateTexture2D(&desc, nullptr, &g_pOverlayTexture);
    if (FAILED(hr)) { Log("InitializeOverlayDirectX: Failed to create overlay texture. HRESULT: " + std::to_string(hr)); CleanupOverlayDirectX(); return false; }
    Log("InitializeOverlayDirectX: Overlay Texture (g_pOverlayTexture) created.");
    Log("InitializeOverlayDirectX: Overlay Texture creation attempted.");

    // --- Get DXGI Surface from Texture ---
    hr = g_pOverlayTexture->QueryInterface(__uuidof(IDXGISurface), reinterpret_cast<void**>(&g_pDXGISurface));
    if (FAILED(hr)) { Log("InitializeOverlayDirectX: Failed to get DXGI surface from overlay texture. HRESULT: " + std::to_string(hr)); CleanupOverlayDirectX(); return false; }
    Log("InitializeOverlayDirectX: QI for DXGI Surface (g_pDXGISurface) succeeded.");
    Log("InitializeOverlayDirectX: DXGI Surface acquisition attempted.");

    // --- Create D2D Bitmap Target from the DXGI Surface ---
    D2D1_BITMAP_PROPERTIES1 bitmapProps = {};
    bitmapProps.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    bitmapProps.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    bitmapProps.dpiX = 96.0f;
    bitmapProps.dpiY = 96.0f;
    bitmapProps.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    hr = g_pD2DContext->CreateBitmapFromDxgiSurface(g_pDXGISurface, &bitmapProps, &g_pD2DTargetBitmap);
    if (FAILED(hr) || !g_pD2DTargetBitmap) {
        Log("InitializeOverlayDirectX: Failed CreateBitmapFromDxgiSurface. HRESULT: " + std::to_string(hr));
        CleanupOverlayDirectX();
        return false;
    }
    Log("InitializeOverlayDirectX: D2D Target Bitmap (g_pD2DTargetBitmap) created.");
    Log("InitializeOverlayDirectX: D2D Bitmap Target creation attempted.");

    g_pD2DContext->SetTarget(g_pD2DTargetBitmap);
    Log("InitializeOverlayDirectX: D2D Context target set.");

    // --- Create D2D / DWrite resources (brushes, text formats) ---
    hr = g_pD2DContext->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &g_pWhiteBrush);
    if (FAILED(hr)) {
        Log("InitializeOverlayDirectX: Failed CreateSolidColorBrush (White). HRESULT: "+std::to_string(hr));
        CleanupOverlayDirectX();
        return false;
    }
    Log("InitializeOverlayDirectX: White Brush created.");

    hr = g_pD2DContext->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &pBlackBrush);
    if (FAILED(hr)) {
        Log("InitializeOverlayDirectX: Failed CreateSolidColorBrush (Black). HRESULT: " + std::to_string(hr));
        CleanupOverlayDirectX();
        return false;
    }
    Log("InitializeOverlayDirectX: Black Brush created.");

    // Create brush for scroll arrows (LightGray)
    hr = g_pD2DContext->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::LightGray), &g_pArrowBrush);
    if (FAILED(hr)) {
        Log("InitializeOverlayDirectX: Failed CreateSolidColorBrush (Arrow). HRESULT: " + std::to_string(hr));
        CleanupOverlayDirectX();
        return false;
    }
    Log("InitializeOverlayDirectX: Arrow Brush created.");

    // --- Create TextFormat for Category List ---
    hr = g_pDWriteFactory->CreateTextFormat(L"Segoe UI", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 24.0f, L"en-us", &g_pTextFormatCategoryList);
    if (FAILED(hr)) { Log("InitializeOverlayDirectX: Failed to create category list text format. HRESULT: " + std::to_string(hr)); CleanupOverlayDirectX(); return false; }
    g_pTextFormatCategoryList->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    g_pTextFormatCategoryList->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    g_pTextFormatCategoryList->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP); // <<< ADD THIS
    Log("InitializeOverlayDirectX: Created TextFormat CategoryList.");

    hr = g_pDWriteFactory->CreateTextFormat(L"Segoe UI", NULL, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 72.0f, L"en-us", &g_pTextFormatStatus);
    if (FAILED(hr)) { Log("InitializeOverlayDirectX: Failed to create main text format. HRESULT: " + std::to_string(hr)); CleanupOverlayDirectX(); return false; }
    g_pTextFormatStatus->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    g_pTextFormatStatus->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    Log("InitializeOverlayDirectX: Created TextFormat Status.");

    // Create text format for Session Time (Top Right, Larger)
    hr = g_pDWriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, // Make it bold
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        48.0f, // Increase font size (e.g., 48.0f)
        L"en-us",
        &g_pTextFormatSessionTime);
    if (FAILED(hr)) {
        Log("InitializeOverlayDirectX: Failed CreateTextFormat SessionTime. HRESULT: " + std::to_string(hr));
        CleanupOverlayDirectX(); return false;
    }
    // Align text to the right (Trailing) and top (Near)
    g_pTextFormatSessionTime->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    g_pTextFormatSessionTime->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    Log("InitializeOverlayDirectX: Created TextFormat SessionTime.");

    hr = g_pDWriteFactory->CreateTextFormat(L"Segoe UI", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 28.0f, L"en-us", &g_pTextFormatGameList);
    if (FAILED(hr)) { Log("InitializeOverlayDirectX: Failed to create game list text format. HRESULT: " + std::to_string(hr)); CleanupOverlayDirectX(); return false; }
    g_pTextFormatGameList->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    g_pTextFormatGameList->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    g_pTextFormatGameList->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP); // <<< ADDED
    Log("InitializeOverlayDirectX: Created TextFormat GameList with word wrapping.");

    hr = g_pDWriteFactory->CreateTextFormat(L"Segoe UI", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 20.0f, L"en-us", &g_pTextFormatGameDesc);
    if (FAILED(hr)) { Log("InitializeOverlayDirectX: Failed to create game desc text format. HRESULT: " + std::to_string(hr)); CleanupOverlayDirectX(); return false; }
    g_pTextFormatGameDesc->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    g_pTextFormatGameDesc->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    g_pTextFormatGameDesc->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    Log("InitializeOverlayDirectX: Created TextFormat GameDesc.");

    hr = g_pD2DContext->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Red), &g_pRedBrush);
    if (FAILED(hr)) {
        Log("InitializeOverlayDirectX: Failed CreateSolidColorBrush (Red). HRESULT: " + std::to_string(hr));
        CleanupOverlayDirectX();
        return false;
    }
    Log("InitializeOverlayDirectX: Red Brush created.");

    // Create TextFormat for Quit Game Button
    hr = g_pDWriteFactory->CreateTextFormat(
        L"Segoe UI",
        NULL,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        20.0f, // Slightly larger font size for "Quit Game"
        L"en-us",
        &g_pTextFormatQuitButton
    );
    if (FAILED(hr)) {
        Log("InitializeOverlayDirectX: Failed CreateTextFormat (QuitButton). HRESULT: " + std::to_string(hr));
        CleanupOverlayDirectX();
        return false;
    }
    g_pTextFormatQuitButton->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    g_pTextFormatQuitButton->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    Log("InitializeOverlayDirectX: QuitButton TextFormat created.");

    // Load scroll arrow images
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    PathRemoveFileSpecW(exePath);
    std::wstring upArrowPath = std::wstring(exePath) + L"\\media\\up.png";
    std::wstring downArrowPath = std::wstring(exePath) + L"\\media\\down.png";
    
    LoadImageFromFileToD2DBitmap(upArrowPath, &g_pUpArrowBitmap);
    LoadImageFromFileToD2DBitmap(downArrowPath, &g_pDownArrowBitmap);

    Log("InitializeOverlayDirectX: Initialization appears successful.");
    return true;
}

// --- OpenVR and Overlay Logic ---

bool InitializeOpenVRForOverlay()
{
    static bool initialized = false;
    if (initialized) return true;

    vr::EVRInitError eError = vr::VRInitError_None;
    vr::VR_Init(&eError, vr::VRApplication_Overlay);
    if (eError != vr::VRInitError_None)
    {
        const char* errMsg = vr::VR_GetVRInitErrorAsEnglishDescription(eError);
        Log(std::string("OpenVR Init Error: ") + errMsg);
        return false;
    }

    Log("OpenVR initialized in Overlay mode.");
    initialized = true;
    return true;
}

// --- Helper: UpdateCurrentCategoryGameList_VR ---
void UpdateCurrentCategoryGameList_VR() {
    std::lock_guard<std::mutex> cat_lock(g_categoriesMutex);
    std::lock_guard<std::mutex> vr_list_lock(g_VROverlayGameListMutex);
    g_currentCategoryGameAppIds_VR.clear();
    g_selectedGameIndex = -1;
    g_gameListScrollOffset = 0;
    if (g_selectedCategoryIndexVR >= 0 && static_cast<size_t>(g_selectedCategoryIndexVR) < g_categories.size()) {
        if (g_categories[g_selectedCategoryIndexVR]) {
            g_currentCategoryGameAppIds_VR = g_categories[g_selectedCategoryIndexVR]->gameAppIds;
            Log("UpdateCurrentCategoryGameList_VR: Switched to category '" + g_categories[g_selectedCategoryIndexVR]->name + "', found " + std::to_string(g_currentCategoryGameAppIds_VR.size()) + " games.");
        } else {
            Log("UpdateCurrentCategoryGameList_VR: Selected category index " + std::to_string(g_selectedCategoryIndexVR) + " points to a null category unique_ptr.");
        }
    } else {
        Log("UpdateCurrentCategoryGameList_VR: No category selected or index out of bounds (" + std::to_string(g_selectedCategoryIndexVR) + "). Game list will be empty.");
    }
    SAFE_RELEASE(g_pDescTextLayout);
    g_descScrollOffsetPx = 0;
    g_currentLayoutAppId = "";
    SAFE_RELEASE(g_pSelectedGameHeader);
    g_isLoadingGameData = false;
}

// --- Helper: Find game by appId ---
// (Function definition removed; now only in steam_games.cpp)
// ... existing code ...

// Helper function to draw overlay content using Direct2D
bool DrawOverlayContentD2D()
{
    if (!g_pD2DContext || !g_pWhiteBrush || !g_pTextFormatStatus || !g_pTextFormatSessionTime || !g_pTextFormatGameList || !g_pTextFormatGameDesc) return false;
    g_pD2DContext->BeginDraw();
    g_pD2DContext->Clear(D2D1::ColorF(0.15f, 0.15f, 0.15f, 1.0f));

    // --- Draw Video Background ---
    if (!g_SessionRunning && g_videoInitialized && g_pVideoFrameBitmap) { // Check if session is NOT running
        // Log("DrawOverlayContentD2D: Drawing video frame bitmap (Waiting for Session)."); // Optional: for debugging
        D2D1_RECT_F videoDestRect = D2D1::RectF(0.0f, 0.0f, (float)OVERLAY_WIDTH, (float)OVERLAY_HEIGHT);
        g_pD2DContext->DrawBitmap(g_pVideoFrameBitmap, videoDestRect);
    } else if (!g_SessionRunning && g_videoInitialized && !g_pVideoFrameBitmap) { // Also ensure session not running for this log
        // Video is supposed to be initialized, but we don't have a frame yet for the "Waiting for session" screen.
        // Log("DrawOverlayContentD2D: Video initialized but g_pVideoFrameBitmap is null (Waiting for session)."); // Optional
    }

    if (!g_SessionRunning) {
        // --- SESSION NOT RUNNING: Draw "Waiting for session..." ---
        std::wstring waitingMsg = L"Waiting for session...";
        D2D1_RECT_F centerRect = D2D1::RectF(0, 0, (float)OVERLAY_WIDTH, (float)OVERLAY_HEIGHT);
        g_pTextFormatStatus->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        g_pTextFormatStatus->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        g_pD2DContext->DrawTextW(waitingMsg.c_str(), (UINT32)waitingMsg.length(), g_pTextFormatStatus, centerRect, g_pWhiteBrush);
    } else {
        // --- SESSION IS RUNNING: Draw Game Launcher UI ---
        // --- CATEGORY LIST DRAWING ---
        std::unique_lock<std::mutex> cat_lock_guard(g_categoriesMutex);
        size_t numTotalCategories = g_categories.size();
        float listTextPadding = 10.0f; // Horizontal padding for text
        float interItemSpacing = 8.0f;  // Vertical space between items

        // Scroll Button Visibility
        if (g_categoryListScrollOffset > 0) {
            if (g_pUpArrowBitmap) {
                g_pD2DContext->DrawBitmap(g_pUpArrowBitmap, g_rectCatScrollUpButton);
            } else { // Fallback
                g_pD2DContext->FillRectangle(g_rectCatScrollUpButton, g_pArrowBrush);
                g_pTextFormatCategoryList->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                g_pTextFormatCategoryList->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                g_pD2DContext->DrawText(L"▲", 1, g_pTextFormatCategoryList, g_rectCatScrollUpButton, g_pWhiteBrush);
            }
        }
        // Down arrow visibility will be determined after drawing items.

        float currentY = g_rectCategoryList.top;
        int itemsDrawn = 0;

        for (int i = 0; ; ++i) {
            int actualCatIndex = g_categoryListScrollOffset + i;
            if (actualCatIndex < 0 || static_cast<size_t>(actualCatIndex) >= numTotalCategories) {
                break; // No more categories
            }
            if (!g_categories[actualCatIndex]) continue;

            std::wstring wCategoryName = StringToWString(g_categories[actualCatIndex]->name);
            if (wCategoryName.empty()) wCategoryName = L"(Unnamed Category)";

            float textLayoutBoxWidth = g_rectCategoryList.right - g_rectCategoryList.left - (listTextPadding * 2);
            if (textLayoutBoxWidth <= 0) textLayoutBoxWidth = 1.0f;

            IDWriteTextLayout* pCategoryNameTextLayout = nullptr;
            HRESULT hr_layout = g_pDWriteFactory->CreateTextLayout(
                wCategoryName.c_str(),
                static_cast<UINT32>(wCategoryName.length()),
                g_pTextFormatCategoryList, // Has word wrapping now
                textLayoutBoxWidth,
                g_rectCategoryList.bottom - currentY, // Max height
                &pCategoryNameTextLayout
            );

            if (SUCCEEDED(hr_layout) && pCategoryNameTextLayout) {
                DWRITE_TEXT_METRICS textMetrics;
                pCategoryNameTextLayout->GetMetrics(&textMetrics);
                float requiredTextHeight = textMetrics.height;

                if (currentY + requiredTextHeight > g_rectCategoryList.bottom && i > 0) {
                    SAFE_RELEASE(pCategoryNameTextLayout);
                    break; // Stop drawing, list area is full
                }

                float currentItemDisplayHeight = requiredTextHeight;

                D2D1_RECT_F itemVisualBound = D2D1::RectF(
                    g_rectCategoryList.left,
                    currentY,
                    g_rectCategoryList.right,
                    currentY + currentItemDisplayHeight
                );

                if (actualCatIndex == g_selectedCategoryIndexVR) {
                    D2D1_COLOR_F highlightColor = D2D1::ColorF(D2D1::ColorF::DodgerBlue, 0.5f);
                    ID2D1SolidColorBrush* pTempHighlightBrush = nullptr;
                    if (SUCCEEDED(g_pD2DContext->CreateSolidColorBrush(highlightColor, &pTempHighlightBrush))) {
                        g_pD2DContext->FillRectangle(itemVisualBound, pTempHighlightBrush);
                        SAFE_RELEASE(pTempHighlightBrush);
                    }
                }

                // Draw the text layout
                g_pD2DContext->DrawTextLayout(
                    D2D1::Point2F(g_rectCategoryList.left + listTextPadding, currentY),
                    pCategoryNameTextLayout,
                    g_pWhiteBrush,
                    D2D1_DRAW_TEXT_OPTIONS_CLIP
                );

                // Draw a separator line
                if (g_pWhiteBrush) {
                    D2D1_POINT_2F lineStart = D2D1::Point2F(g_rectCategoryList.left, currentY + currentItemDisplayHeight + (interItemSpacing / 2.0f) - 1.0f);
                    D2D1_POINT_2F lineEnd = D2D1::Point2F(g_rectCategoryList.right, currentY + currentItemDisplayHeight + (interItemSpacing / 2.0f) - 1.0f);
                    g_pD2DContext->DrawLine(lineStart, lineEnd, g_pWhiteBrush, 0.5f);
                }

                currentY += currentItemDisplayHeight + interItemSpacing;
                itemsDrawn++;
            }

            SAFE_RELEASE(pCategoryNameTextLayout);
            if (currentY >= g_rectCategoryList.bottom) {
                break;
            }
        } // End for loop for category items

        // Now draw the down arrow if needed
        if (static_cast<size_t>(g_categoryListScrollOffset + itemsDrawn) < numTotalCategories) {
            if (g_pDownArrowBitmap) {
                g_pD2DContext->DrawBitmap(g_pDownArrowBitmap, g_rectCatScrollDownButton);
            } else { // Fallback
                g_pD2DContext->FillRectangle(g_rectCatScrollDownButton, g_pArrowBrush);
                g_pTextFormatCategoryList->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                g_pTextFormatCategoryList->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                g_pD2DContext->DrawText(L"▼", 1, g_pTextFormatCategoryList, g_rectCatScrollDownButton, g_pWhiteBrush);
            }
        }

        // Reset text alignment for other UI elements that might use this format
        g_pTextFormatCategoryList->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        g_pTextFormatCategoryList->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

        cat_lock_guard.unlock();
        // --- GAME LIST (Filtered by Category) ---
        std::vector<std::string> currentGamesInVRList;
        {
            std::lock_guard<std::mutex> vr_list_lock(g_VROverlayGameListMutex);
            currentGamesInVRList = g_currentCategoryGameAppIds_VR;
        }
        if (g_selectedCategoryIndexVR != -1 && !currentGamesInVRList.empty()) {
            size_t numTotalGamesInCategory = currentGamesInVRList.size();
            float listTextPadding = 10.0f; // Horizontal padding for text within the list item
            float interItemSpacing = 8.0f; // Vertical space between list items

            // Estimate max visible games for scroll button visibility (can be rough)
            // This might need refinement if average item height varies wildly.
            // Consider the smallest possible item height (e.g., one line of text + spacing)
            float minItemHeightEstimate = 30.0f + interItemSpacing; 
            int estimatedMaxVisible = 0;
            if (minItemHeightEstimate > 0) {
                estimatedMaxVisible = static_cast<int>((g_rectGameList.bottom - g_rectGameList.top) / minItemHeightEstimate);
            }

            // This drawing logic for scroll buttons is being replaced with a more accurate version below.

            // PERFORMANCE: Pre-fetch all game pointers to avoid repeated findGameByAppId calls
            std::vector<SteamGame*> visibleGamePtrs;
            visibleGamePtrs.reserve(8); // Reserve space for typical number of visible games
            
            // This loop determines which games are visible based on the current scroll offset.
            for (size_t idx = g_gameListScrollOffset; idx < numTotalGamesInCategory && visibleGamePtrs.size() < 8; ++idx) {
                const std::string& gameAppId = currentGamesInVRList[idx];
                SteamGame* game = findGameByAppId(gameAppId);
                if (game) {
                    visibleGamePtrs.push_back(game);
                }
            }

            float currentY = g_rectGameList.top;
            int itemsDrawn = 0; // Counter for actually drawn items
            bool downArrowVisible = false; // Flag to track if the down arrow should be visible

            for (size_t i = 0; i < visibleGamePtrs.size(); ++i) {
                SteamGame* game = visibleGamePtrs[i];

                std::wstring wGameName = StringToWString(game->name);
                if (wGameName.empty()) wGameName = L"(Unnamed Game)"; // Placeholder for empty names

                float textLayoutBoxWidth = (g_rectGameList.right) - (g_rectGameList.left + listTextPadding * 2); // Width available for text
                if (textLayoutBoxWidth <= 0) textLayoutBoxWidth = 1.0f;

                IDWriteTextLayout* pGameNameTextLayout = nullptr;
                HRESULT hr_layout = g_pDWriteFactory->CreateTextLayout(
                    wGameName.c_str(),
                    static_cast<UINT32>(wGameName.length()),
                    g_pTextFormatGameList, // Word wrapping is enabled on this
                    textLayoutBoxWidth,
                    g_rectGameList.bottom - currentY, // Max height is remaining space in the list box
                    &pGameNameTextLayout
                );

                if (SUCCEEDED(hr_layout) && pGameNameTextLayout) {
                    DWRITE_TEXT_METRICS textMetrics;
                    pGameNameTextLayout->GetMetrics(&textMetrics);
                    float requiredTextHeight = textMetrics.height;

                    // If this item (even just its text) won't fit and it's not the first one we're trying to draw in the visible list
                    if (currentY + requiredTextHeight > g_rectGameList.bottom && i > 0) {
                        SAFE_RELEASE(pGameNameTextLayout);
                        break; // Stop drawing, list area is full
                    }
                    // If it's the first item (i=0) and it overflows, it will be clipped by DrawTextLayout with D2D1_DRAW_TEXT_OPTIONS_CLIP.

                    float currentItemDisplayHeight = requiredTextHeight; // The actual height the text will take

                    // Define the bounding box for highlighting and interaction for this item
                    D2D1_RECT_F itemVisualBound = D2D1::RectF(
                        g_rectGameList.left,
                        currentY,
                        g_rectGameList.right,
                        currentY + currentItemDisplayHeight // Highlight uses the actual text height
                    );

                    int actualGameListIndex = g_gameListScrollOffset + static_cast<int>(i);
                    if (actualGameListIndex == g_selectedGameIndex) {
                        D2D1_COLOR_F gameHighlightColor = D2D1::ColorF(D2D1::ColorF::DarkSlateGray, 0.7f);
                        ID2D1SolidColorBrush* pGameHighlightBrush = nullptr;
                        if (SUCCEEDED(g_pD2DContext->CreateSolidColorBrush(gameHighlightColor, &pGameHighlightBrush))) {
                            g_pD2DContext->FillRectangle(itemVisualBound, pGameHighlightBrush);
                            SAFE_RELEASE(pGameHighlightBrush);
                        }
                    }

                    // Draw the text layout object
                    g_pD2DContext->DrawTextLayout(
                        D2D1::Point2F(g_rectGameList.left + listTextPadding, currentY), // Origin for text drawing
                        pGameNameTextLayout,
                        g_pWhiteBrush,
                        D2D1_DRAW_TEXT_OPTIONS_CLIP // Clip text to the layout box defined in CreateTextLayout
                    );

                    // --- BEGINNING OF NEW CODE ---
                    // Draw a separator line below the item, but before the interItemSpacing
                    if (g_pWhiteBrush) { // Use an existing brush, or create a specific one
                        D2D1_POINT_2F lineStart = D2D1::Point2F(g_rectGameList.left, currentY + currentItemDisplayHeight + (interItemSpacing / 2.0f) - 1.0f ); // Position slightly above the full spacing
                        D2D1_POINT_2F lineEnd = D2D1::Point2F(g_rectGameList.right, currentY + currentItemDisplayHeight + (interItemSpacing / 2.0f) - 1.0f);
                        g_pD2DContext->DrawLine(lineStart, lineEnd, g_pWhiteBrush, 0.5f); // Use a thin line
                    }
                    // --- END OF NEW CODE ---

                    currentY += currentItemDisplayHeight + interItemSpacing; // Advance Y for the next item
                    itemsDrawn++; // Increment after successfully processing an item that fits
                }
                SAFE_RELEASE(pGameNameTextLayout);

                if (currentY >= g_rectGameList.bottom) {
                    break; // List area full
                }
            } // End for loop for game items

            g_lastVisibleGameCount = itemsDrawn; // Track how many games are visible

            // NEW SCROLL BUTTON DRAWING LOGIC:
            // This is now done *after* the list is drawn, so we know exactly if buttons are needed.
            if (g_gameListScrollOffset > 0) { // If we've scrolled down at all, show the up arrow.
                if (g_pUpArrowBitmap) {
                    g_pD2DContext->DrawBitmap(g_pUpArrowBitmap, g_rectScrollUpButton);
                } else { // Fallback to text if image fails to load
                    g_pD2DContext->FillRectangle(g_rectScrollUpButton, g_pArrowBrush);
                    g_pTextFormatGameList->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    g_pTextFormatGameList->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    g_pD2DContext->DrawText(L"▲", 1, g_pTextFormatGameList, g_rectScrollUpButton, g_pWhiteBrush);
                }
            }
            if (g_gameListScrollOffset + itemsDrawn < numTotalGamesInCategory) { // If there are more items below, show down arrow.
                downArrowVisible = true;
                if (g_pDownArrowBitmap) {
                    g_pD2DContext->DrawBitmap(g_pDownArrowBitmap, g_rectScrollDownButton);
                } else { // Fallback to text
                    g_pD2DContext->FillRectangle(g_rectScrollDownButton, g_pArrowBrush);
                    g_pTextFormatGameList->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    g_pTextFormatGameList->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    g_pD2DContext->DrawText(L"▼", 1, g_pTextFormatGameList, g_rectScrollDownButton, g_pWhiteBrush);
                }
            }
            // Reset text alignment for other UI elements
            g_pTextFormatGameList->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            g_pTextFormatGameList->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

            // --- DETAILS PANEL (for selected game in the current category's list) ---
            // This logic remains largely the same, it's just placed after the game list drawing
            if (g_selectedGameIndex >= 0 && static_cast<size_t>(g_selectedGameIndex) < numTotalGamesInCategory) {
                const std::string& selectedGameAppIdFromList = currentGamesInVRList[g_selectedGameIndex];
                SteamGame* selectedGameDetails = findGameByAppId(selectedGameAppIdFromList);
                if (selectedGameDetails) {
                    std::unique_lock<std::mutex> overlayLock(g_overlayStateMutex);
                    // Draw Header Image
                    if (g_pSelectedGameHeader) {
                        g_pD2DContext->DrawBitmap(g_pSelectedGameHeader, g_rectHeaderImage);
                    } else if (g_isLoadingGameData) {
                        g_pTextFormatGameDesc->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                        g_pTextFormatGameDesc->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                        g_pD2DContext->DrawText(L"Loading Data...", wcslen(L"Loading Data..."), g_pTextFormatGameDesc, g_rectHeaderImage, g_pWhiteBrush);
                        g_pTextFormatGameDesc->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                        g_pTextFormatGameDesc->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
                    }
                    // Draw Description (g_pTextFormatGameDesc already has DWRITE_WORD_WRAPPING_WRAP)
                    if (g_currentLayoutAppId != selectedGameDetails->appid || !g_pDescTextLayout) {
                        SAFE_RELEASE(g_pDescTextLayout);
                        g_descScrollOffsetPx = 0;
                        g_currentLayoutAppId = "";
                        if (g_pDWriteFactory && g_pTextFormatGameDesc && !selectedGameDetails->description.empty()) {
                            std::wstring desc_w = StringToWString(selectedGameDetails->description);
                            g_pDWriteFactory->CreateTextLayout(
                                desc_w.c_str(), (UINT32)desc_w.length(), g_pTextFormatGameDesc,
                                g_rectDescription.right - g_rectDescription.left,
                                g_rectDescription.bottom - g_rectDescription.top,
                                &g_pDescTextLayout);
                            if (g_pDescTextLayout) {
                                g_currentLayoutAppId = selectedGameDetails->appid;
                            }
                        }
                    }
                    if (g_pDescTextLayout) {
                        D2D1_POINT_2F origin = D2D1::Point2F(g_rectDescription.left, g_rectDescription.top - g_descScrollOffsetPx);
                        g_pD2DContext->DrawTextLayout(origin, g_pDescTextLayout, g_pWhiteBrush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
                        DWRITE_TEXT_METRICS textMetrics;
                        if (SUCCEEDED(g_pDescTextLayout->GetMetrics(&textMetrics))) {
                            if (textMetrics.layoutHeight > (g_rectDescription.bottom - g_rectDescription.top)) {
                                g_pD2DContext->FillRectangle(g_rectDescScrollUpButton, g_pArrowBrush);
                                g_pD2DContext->DrawText(L"↑", 1, g_pTextFormatGameList, g_rectDescScrollUpButton, g_pWhiteBrush);
                                g_pD2DContext->FillRectangle(g_rectDescScrollDownButton, g_pArrowBrush);
                                g_pD2DContext->DrawText(L"↓", 1, g_pTextFormatGameList, g_rectDescScrollDownButton, g_pWhiteBrush);
                            }
                        }
                    }
                    // Draw Start Button
                    g_pD2DContext->FillRectangle(g_rectStartButton, g_pWhiteBrush);
                    IDWriteTextLayout* pStartButtonTextLayout = nullptr;
                    
                    // Replace the static text with a dynamic check
                    std::wstring startButtonText;
                    if (selectedGameDetails && g_launchingAppId == selectedGameDetails->appid) {
                        startButtonText = L"Launching...";
                    } else {
                        startButtonText = L"Start Game";
                    }

                    if (g_pDWriteFactory && g_pTextFormatGameList) { // g_pTextFormatGameList is fine here as a base
                        g_pDWriteFactory->CreateTextLayout(startButtonText.c_str(), static_cast<UINT32>(startButtonText.length()),
                            g_pTextFormatGameList, g_rectStartButton.right - g_rectStartButton.left,
                            g_rectStartButton.bottom - g_rectStartButton.top, &pStartButtonTextLayout);
                        if (pStartButtonTextLayout) {
                            pStartButtonTextLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                            pStartButtonTextLayout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                            g_pD2DContext->DrawTextLayout(D2D1::Point2F(g_rectStartButton.left, g_rectStartButton.top), pStartButtonTextLayout, pBlackBrush);
                            SAFE_RELEASE(pStartButtonTextLayout);
                        }
                    }
                    // Draw Quit Game Button (to the right of Start Game button)
                    if (g_pRedBrush && g_pTextFormatQuitButton) { // Ensure brushes and new text format are created
                        g_pD2DContext->FillRectangle(g_rectQuitButton, g_pRedBrush);
                        IDWriteTextLayout* pQuitButtonTextLayout = nullptr;
                        std::wstring quitButtonText = L"Quit\nGame"; // Text with newline for two lines

                        if (g_pDWriteFactory) {
                            g_pDWriteFactory->CreateTextLayout(
                                quitButtonText.c_str(),
                                static_cast<UINT32>(quitButtonText.length()),
                                g_pTextFormatQuitButton, // Use the new dedicated text format
                                g_rectQuitButton.right - g_rectQuitButton.left, // Max width for layout
                                g_rectQuitButton.bottom - g_rectQuitButton.top, // Max height for layout
                                &pQuitButtonTextLayout
                            );

                            if (pQuitButtonTextLayout) {
                                // Adjust line spacing to bring lines closer
                                pQuitButtonTextLayout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, 18.0f, 15.0f); // Adjusted line spacing & baseline for larger font

                                // Alignment is already set on g_pTextFormatQuitButton, no need to set again on layout usually.
                                g_pD2DContext->DrawTextLayout(
                                    D2D1::Point2F(g_rectQuitButton.left, g_rectQuitButton.top),
                                    pQuitButtonTextLayout,
                                    g_pWhiteBrush // Text color
                                );
                                SAFE_RELEASE(pQuitButtonTextLayout);
                            }
                        }
                    }
                }
            }
        } else if (g_selectedCategoryIndexVR != -1) { // Category selected, but it's empty
            g_pTextFormatGameList->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            g_pTextFormatGameList->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            g_pD2DContext->DrawText(L"No Games in Category", wcslen(L"No Games in Category"), g_pTextFormatGameList, g_rectGameList, g_pWhiteBrush);
            g_pTextFormatGameList->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING); // Reset alignment
            g_pTextFormatGameList->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR); // Reset alignment
        }
    }
    // --- Draw Session Timer (Always Visible) ---
    if (g_pTextFormatSessionTime && g_pWhiteBrush) {
        wchar_t timeText[64];
        int displaySeconds = g_SessionTimeSeconds;
        if (displaySeconds < 0) displaySeconds = 0;
        int mins = displaySeconds / 60;
        int secs = displaySeconds % 60;
        swprintf_s(timeText, L"%d:%02d", mins, secs);
        D2D1_RECT_F timeRect = D2D1::RectF(
            OVERLAY_WIDTH - 220.0f,
            10.0f,
            OVERLAY_WIDTH - 10.0f,
            70.0f
        );
        g_pTextFormatSessionTime->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        g_pTextFormatSessionTime->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        g_pD2DContext->DrawTextW(timeText, (UINT32)wcslen(timeText), g_pTextFormatSessionTime, timeRect, g_pWhiteBrush);
    }
    HRESULT hr = g_pD2DContext->EndDraw();
    if (FAILED(hr)) {
        Log("DrawOverlayContentD2D -> EndDraw FAILED. HRESULT: " + std::to_string(hr));
        if (hr == D2DERR_RECREATE_TARGET) {
            Log("DrawOverlayContentD2D -> D2DERR_RECREATE_TARGET received. Cleaning up DirectX resources and flagging for re-initialization.");
            CleanupOverlayDirectX(); // This makes g_pD3DDevice, g_pD2DContext, etc., nullptr.
            // Set continuousOverlayRunning to false. The next ShowOverlayContinuous call
            // (triggered by WM_APP_ENSURE_OVERLAY_VISIBLE from the persistent timer)
            // will see this and g_pD3DDevice == nullptr, then perform a full re-initialization
            // of DirectX resources via PrepareOverlay.
            continuousOverlayRunning = false;
        }
        return false;
    }
    return true;
}

// UpdateOverlayTexture now takes no text parameter
bool UpdateOverlayTexture(vr::VROverlayHandle_t overlayHandle)
{
    vr::IVROverlay* pOverlay = vr::VROverlay();
    if (!pOverlay || !g_pOverlayTexture || !g_pImmediateContext || !g_pD2DContext || !g_pD2DTargetBitmap || !g_pWhiteBrush || !pBlackBrush || !g_pTextFormatGameList || !g_pTextFormatGameDesc || !g_pTextFormatStatus || !g_pTextFormatSessionTime)
    {
        std::string missing = "Missing: ";
        if (!pOverlay) missing += "IVROverlay ";
        if (!g_pOverlayTexture) missing += "OverlayTexture ";
        if (!g_pImmediateContext) missing += "ImmediateContext ";
        if (!g_pD2DContext) missing += "D2DContext ";
        if (!g_pD2DTargetBitmap) missing += "D2DTargetBitmap ";
        if (!g_pWhiteBrush) missing += "WhiteBrush ";
        if (!pBlackBrush) missing += "BlackBrush ";
        if (!g_pTextFormatGameList) missing += "GameListFormat ";
        if (!g_pTextFormatGameDesc) missing += "GameDescFormat ";
        if (!g_pTextFormatStatus) missing += "StatusFormat ";
        if (!g_pTextFormatSessionTime) missing += "SessionTimeFormat ";
        Log("UpdateOverlayTexture -> Prerequisites not ready. " + missing);
        return false;
    }
    // --- FIX 3: Set D2D target before drawing ---
    g_pD2DContext->SetTarget(g_pD2DTargetBitmap);
    
    // Use the "Waiting for session" as default status text
    wchar_t statusText[128] = L"Waiting for session...";
    
    // --- Video Frame Swap Logic ---
    if (g_videoInitialized) {
        std::unique_lock<std::mutex> lock(g_videoFrameMutex);
        if (g_rawVideoFrameReady && g_pD2DContext) {
            if (!g_rawVideoFrameBuffer.empty() && g_rawVideoFrameWidth > 0 && g_rawVideoFrameHeight > 0) {
                D2D1_SIZE_U newSize = D2D1::SizeU(g_rawVideoFrameWidth, g_rawVideoFrameHeight);
                D2D1_SIZE_U currentBitmapSize = {0, 0};
                if (g_pVideoFrameBitmap) {
                    currentBitmapSize = g_pVideoFrameBitmap->GetPixelSize();
                }
                if (!g_pVideoFrameBitmap || currentBitmapSize.width != newSize.width || currentBitmapSize.height != newSize.height) {
                    SAFE_RELEASE(g_pVideoFrameBitmap);
                    D2D1_BITMAP_PROPERTIES props = {};
                    props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
                    props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
                    props.dpiX = 96.0f;
                    props.dpiY = 96.0f;
                    HRESULT hr_create = g_pD2DContext->CreateBitmap(newSize, props, &g_pVideoFrameBitmap);
                    if (FAILED(hr_create)) {
                        Log("UpdateOverlayTexture: Failed to create/recreate g_pVideoFrameBitmap. HR=" + std::to_string(hr_create));
                        SAFE_RELEASE(g_pVideoFrameBitmap);
                    }
                }
                if (g_pVideoFrameBitmap) {
                    D2D1_RECT_U rect = D2D1::RectU(0, 0, newSize.width, newSize.height);
                    HRESULT hr_copy = g_pVideoFrameBitmap->CopyFromMemory(&rect, g_rawVideoFrameBuffer.data(), g_rawVideoFrameWidth * 4);
                    if (FAILED(hr_copy)) {
                        Log("UpdateOverlayTexture: Failed to copy raw frame to g_pVideoFrameBitmap. HR=" + std::to_string(hr_copy));
                    }
                }
            }
            g_rawVideoFrameReady = false;
        }
        lock.unlock();
    }
    
    if (!DrawOverlayContentD2D())
    {
        Log("UpdateOverlayTexture -> Failed to draw content.");
        return false;
    }
    
    // Log("UpdateOverlayTexture -> Flushing immediate context before SetOverlayTexture."); // <--- REMOVED LOG MESSAGE
    // g_pImmediateContext->Flush(); // <--- REMOVED: Redundant GPU sync that causes stuttering
    vr::Texture_t overlayTexture = {};
    overlayTexture.handle = (void*)g_pOverlayTexture;
    overlayTexture.eType = vr::TextureType_DirectX;
    overlayTexture.eColorSpace = vr::ColorSpace_Auto;
    vr::EVROverlayError overlayError = pOverlay->SetOverlayTexture(overlayHandle, &overlayTexture);
    if (overlayError != vr::VROverlayError_None)
    {
        Log("UpdateOverlayTexture -> SetOverlayTexture failed: " + std::string(pOverlay->GetOverlayErrorNameFromEnum(overlayError)));
        return false;
    }
    return true;
}

// RefreshOverlayTexture now takes no text/status
bool RefreshOverlayTexture(vr::VROverlayHandle_t overlayHandle)
{
    if (overlayHandle == 0 || !g_pD3DDevice)
        return false;
    vr::IVROverlay* pOverlay = vr::VROverlay();
    if (!pOverlay)
        return false;
    return UpdateOverlayTexture(overlayHandle);
}

// Time update timer callback - Calls RefreshOverlayTexture
VOID CALLBACK TimeUpdateProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
    static DWORD lastUpdateTime = 0;
    DWORD currentTime = GetTickCount(); // Use GetTickCount for simple interval measurement

    if (lastUpdateTime != 0)
    {
        // Log interval only if it's significantly different from 1000ms, e.g., +/- 100ms
        DWORD interval = currentTime - lastUpdateTime;
        if (interval < 900 || interval > 1100) { // Reduce log spam
            Log("Timer callback interval: " + std::to_string(interval) + "ms");
        }
    }
    lastUpdateTime = currentTime;

    if (g_MainOverlay != 0)
    {
        RefreshOverlayTexture(g_MainOverlay); // Calls the renamed texture refresh function
    }
}

// Add this new function to generate a simple thumbnail icon
bool CreateThumbnailIcon(vr::VROverlayHandle_t overlayHandle)
{
    // ... (this can remain using SetOverlayRaw for simplicity, or be updated to D3D too)
    vr::IVROverlay* pOverlay = vr::VROverlay();
    if (!pOverlay) { Log("CreateThumbnailIcon -> IVROverlay not available."); return false; }
    const int width = 64; const int height = 64;
    unsigned char* buffer = new unsigned char[width * height * 4];
    Log("CreateThumbnailIcon: Filling buffer with solid opaque green for testing.");
    for (int i = 0; i < width * height * 4; i += 4) { buffer[i] = 0; buffer[i + 1] = 255; buffer[i + 2] = 0; buffer[i + 3] = 255; }
    vr::EVROverlayError texErr = pOverlay->SetOverlayRaw(overlayHandle, buffer, width, height, 4);
    delete[] buffer;
    if (texErr != vr::VROverlayError_None) { Log("CreateThumbnailIcon -> error code " + std::to_string((int)texErr)); return false; }
    Log("Thumbnail icon created successfully (solid green).");
    return true;
}

void PrepareOverlay()
{
    if (!InitializeOpenVRForOverlay()) {
        Log("PrepareOverlay -> OpenVR initialization failed, skipping overlay preparation.");
        return;
    }
    vr::IVROverlay* pOverlay = vr::VROverlay();
    if (!pOverlay) {
        Log("PrepareOverlay -> VROverlay system not available.");
        return;
    }

    // Initialize DirectX resources if they are not already.
    if (!g_pD3DDevice) { // If D3D device is null, all D2D/DXGI resources are also considered invalid.
        Log("PrepareOverlay: D3D device is null, calling InitializeOverlayDirectX.");
        if (!InitializeOverlayDirectX()) {
            Log("PrepareOverlay -> CRITICAL: Failed to initialize DirectX resources.");
            return; // Cannot proceed without DirectX.
        }
    } else {
        Log("PrepareOverlay: D3D device already exists.");
    }

    // Initialize video if D3D is ready and video isn't already initialized.
    if (g_pD3DDevice && !g_videoInitialized) {
        Log("PrepareOverlay: Initializing video resources.");
        InitializeVideo(); // InitializeVideo sets g_videoInitialized
    }

    if (g_MainOverlay == 0) {
        Log("PrepareOverlay: g_MainOverlay is 0, creating new dashboard overlay.");
        vr::EVROverlayError overlayError = pOverlay->CreateDashboardOverlay(
            "arcade.station.overlay",
            "Session Timer",
            &g_MainOverlay,
            &g_ThumbnailOverlay);

        if (overlayError != vr::VROverlayError_None) {
            Log("PrepareOverlay: CreateDashboardOverlay FAILED. Error: " + std::to_string((int)overlayError) + " (" + pOverlay->GetOverlayErrorNameFromEnum(overlayError) + ")");
            CleanupOverlayDirectX(); // Clean up any D3D if VR overlay creation failed
            g_MainOverlay = 0; 
            g_ThumbnailOverlay = 0;
            return;
        }
        Log("PrepareOverlay: Dashboard overlay CREATED. Main: " + std::to_string(g_MainOverlay) + ", Thumb: " + std::to_string(g_ThumbnailOverlay));
        
        // Set properties for the newly created overlay
        pOverlay->SetOverlayWidthInMeters(g_MainOverlay, 2.5f);
        pOverlay->SetOverlayFlag(g_MainOverlay, vr::VROverlayFlags_SendVRDiscreteScrollEvents, true); // For mouse wheel scrolling in overlay list
        pOverlay->SetOverlayInputMethod(g_MainOverlay, vr::VROverlayInputMethod_Mouse);
        vr::HmdVector2_t vecWindowSize = { static_cast<float>(OVERLAY_WIDTH), static_cast<float>(OVERLAY_HEIGHT) };
        vr::EVROverlayError mouseScaleErr = pOverlay->SetOverlayMouseScale(g_MainOverlay, &vecWindowSize);
        Log("DEBUG: SetOverlayMouseScale result = " + std::to_string((int)mouseScaleErr) + " for size " + 
            std::to_string(OVERLAY_WIDTH) + "x" + std::to_string(OVERLAY_HEIGHT));
        pOverlay->SetOverlayFlag(g_MainOverlay, vr::VROverlayFlags_VisibleInDashboard, true);
        
        if (g_ThumbnailOverlay != 0) {
             pOverlay->SetOverlayFlag(g_ThumbnailOverlay, vr::VROverlayFlags_VisibleInDashboard, true);
             CreateThumbnailIcon(g_ThumbnailOverlay);
        }

    } else {
        Log("PrepareOverlay: g_MainOverlay (" + std::to_string(g_MainOverlay) + ") already exists. Ensuring flags/settings.");
        // Overlay handles exist. Ensure key flags are set, as SteamVR might sometimes reset them.
        pOverlay->SetOverlayFlag(g_MainOverlay, vr::VROverlayFlags_VisibleInDashboard, true);
        pOverlay->SetOverlayFlag(g_MainOverlay, vr::VROverlayFlags_SendVRDiscreteScrollEvents, true);
        pOverlay->SetOverlayInputMethod(g_MainOverlay, vr::VROverlayInputMethod_Mouse); // Re-apply if needed
        
        // CRITICAL FIX: Re-apply mouse scale to ensure coordinate system consistency
        vr::HmdVector2_t vecWindowSize = { static_cast<float>(OVERLAY_WIDTH), static_cast<float>(OVERLAY_HEIGHT) };
        vr::EVROverlayError mouseScaleErr = pOverlay->SetOverlayMouseScale(g_MainOverlay, &vecWindowSize);
        Log("DEBUG: Re-applied SetOverlayMouseScale result = " + std::to_string((int)mouseScaleErr) + " for size " + 
            std::to_string(OVERLAY_WIDTH) + "x" + std::to_string(OVERLAY_HEIGHT));
            
        if (g_ThumbnailOverlay != 0) {
             pOverlay->SetOverlayFlag(g_ThumbnailOverlay, vr::VROverlayFlags_VisibleInDashboard, true);
        }
    }

    // Ensure the texture is updated after preparing/confirming the overlay.
    if (g_pD3DDevice && g_MainOverlay !=0 && !UpdateOverlayTexture(g_MainOverlay)) {
        Log("PrepareOverlay -> WARNING: Failed to set initial/updated overlay texture.");
    }
    Log("PrepareOverlay: Overlay preparation logic complete.");
}

void ShowOverlayOriginalMethod()
{
    if (g_MainOverlay == 0) {
        Log("ShowOverlayOriginalMethod: Called when g_MainOverlay is 0. Aborting show attempt.");
        return;
    }
    vr::IVROverlay* pOverlay = vr::VROverlay();
    if (!pOverlay) {
        Log("ShowOverlayOriginalMethod -> pOverlay not available.");
        return;
    }

    // Check dashboard visibility *again* right before critical action.
    // This is a more immediate check than the one in the persistent timer.
    if (!pOverlay->IsDashboardVisible()) {
        Log("ShowOverlayOriginalMethod: Dashboard is NOT visible right now. Aborting ShowDashboard() call, will only ensure overlay handles are 'shown'.");
        // We still call ShowOverlay on our handles so if the user manually opens dashboard, our overlay appears.
        pOverlay->ShowOverlay(g_MainOverlay);
        pOverlay->ShowOverlay(g_ThumbnailOverlay);
        // We do NOT call pOverlay->ShowDashboard() here, as it would force the dashboard open.
        return;
    }

    Log("ShowOverlayOriginalMethod: Dashboard is currently visible. Attempting to show overlay handles and ensure our tab is active.");

    vr::EVROverlayError mainErr = pOverlay->ShowOverlay(g_MainOverlay);
    vr::EVROverlayError thumbErr = pOverlay->ShowOverlay(g_ThumbnailOverlay);

    if ((mainErr != vr::VROverlayError_None && mainErr != VROverlayError_AlreadyVisible) ||
        (thumbErr != vr::VROverlayError_None && thumbErr != VROverlayError_AlreadyVisible))
    {
        Log("ShowOverlayOriginalMethod ShowOverlay error: main=" + std::to_string((int)mainErr) +
            ", thumb=" + std::to_string((int)thumbErr));
    } else {
        Log("ShowOverlayOriginalMethod: ShowOverlay() call for handles succeeded or they were already visible.");
    }

    // The ShowDashboard call ensures our overlay is the one brought to the front.
    // Since we've confirmed the dashboard is visible just above, this should be safe.
    const char* overlayKey = "arcade.station.overlay";
    Log("ShowOverlayOriginalMethod: Calling ShowDashboard with key: " + std::string(overlayKey) + " to ensure our tab is active.");
    pOverlay->ShowDashboard(overlayKey);

    static bool firstShowCallLog = true; // Renamed to avoid conflict if 'firstShowCall' is used elsewhere
    if (firstShowCallLog) {
        Log("ShowOverlayOriginalMethod: ShowDashboard called. Overlay should be visible in dashboard and be the active tab.");
        firstShowCallLog = false;
    }
}

void LaunchSteamVR()
{
    Log("Attempting to launch SteamVR...");
    HINSTANCE hInst = ShellExecuteW(NULL, L"open", STEAMVR_PATH, NULL, NULL, SW_SHOW);
    if ((INT_PTR)hInst <= 32)
    {
        Log("Failed to launch SteamVR. ShellExecute error code: " + std::to_string((int)(INT_PTR)hInst));
        MessageBoxW(NULL, L"Failed to launch SteamVR.", L"Error", MB_OK | MB_ICONERROR);
    }
    else
    {
        Log("SteamVR launch initiated.");
    }
}

void ShowOverlayContinuous()
{
    // This function is called when we expect to make the overlay fully visible and active.
    // Safeguard checks, though the persistent timer should gate these.
    if (!IsSteamVRRunning()) {
        Log("ShowOverlayContinuous: SteamVR is NOT running (safeguard). Aborting.");
        return;
    }
    if (!InitializeOpenVRForOverlay() || !vr::VROverlay() || !vr::VROverlay()->IsDashboardVisible()) {
        Log("ShowOverlayContinuous: OpenVR not ready or dashboard not visible (safeguard). Aborting.");
        // If dashboard isn't visible now, tell the main loop to pause rendering parts
        HWND mainWnd = FindWindowW(L"QuitVRAppClass", NULL);
        if(mainWnd) PostMessageW(mainWnd, WM_APP_PAUSE_OVERLAY_RENDERING, 0, 0);
        return;
    }

    // Check if fundamental resources (VR Overlay Handles, D3D Device) exist.
    // If not, it implies a first run or a recovery from a D2DERR_RECREATE_TARGET.
    if (g_MainOverlay == 0 || g_pD3DDevice == nullptr) {
        Log("ShowOverlayContinuous: Core infrastructure (VR Handles or D3D Device) missing. Running PrepareOverlay.");
        
        // Ensure any old state is cleaned before preparing, especially D3D
        CleanupOverlayDirectX(); // This cleans D3D and video, sets g_pD3DDevice = nullptr
        
        // Explicitly destroy VR overlay handles if they somehow exist without D3D device (unlikely but defensive)
        if (g_MainOverlay != 0 && vr::VROverlay()) { vr::VROverlay()->DestroyOverlay(g_MainOverlay); g_MainOverlay = 0; }
        if (g_ThumbnailOverlay != 0 && vr::VROverlay()) { vr::VROverlay()->DestroyOverlay(g_ThumbnailOverlay); g_ThumbnailOverlay = 0; }

        PrepareOverlay(); // This will create VR handles and D3D device if they are null.
        
        if (g_MainOverlay == 0 || g_pD3DDevice == nullptr) {
            Log("ShowOverlayContinuous: CRITICAL FAILURE - PrepareOverlay did not establish core resources.");
            continuousOverlayRunning = false; // Mark that we are not properly running
            return;
        }
        Log("ShowOverlayContinuous: Core infrastructure established by PrepareOverlay.");
        // After a successful PrepareOverlay that created new handles/device,
        // we consider the overlay "running" in terms of setup.
        continuousOverlayRunning = true; 
    }
    // At this point, g_MainOverlay and g_pD3DDevice should be valid.

    // Ensure video is initialized and its processing thread is running.
    if (!g_videoInitialized) { // If video was paused or never started
        if (InitializeVideo()) { // InitializeVideo also sets g_videoInitialized = true
             Log("ShowOverlayContinuous: Video Initialized.");
        } else {
            Log("ShowOverlayContinuous: Video Failed to Initialize. Overlay will lack video background.");
        }
    }
    if (g_videoInitialized && (g_stopVideoThread || !g_videoThread.joinable())) {
        g_stopVideoThread = false; // Clear stop flag
        if (g_videoThread.joinable()) { // If joinable, it means it finished or was detached
             Log("ShowOverlayContinuous: Previous video thread was joinable, attempting join before restart.");
             g_videoThread.join(); // Join it to clean up
        }
        Log("ShowOverlayContinuous: Starting video processing thread.");
        g_videoThread = std::thread(VideoProcessingThread);
    }

    // Ensure the main overlay content refresh timer is running.
    HWND mainWnd = FindWindowW(L"QuitVRAppClass", NULL);
    if (mainWnd) {
        KillTimer(mainWnd, OVERLAY_TIMER_ID); // Kill existing timer to reset it
        if (SetTimer(mainWnd, OVERLAY_TIMER_ID, OVERLAY_REFRESH_INTERVAL, NULL)) {
            // Log("ShowOverlayContinuous: Overlay update timer (OVERLAY_TIMER_ID) started/reset."); // Can be spammy
        } else {
            Log("ShowOverlayContinuous: FAILED to start overlay update timer! GetLastError: " + std::to_string(GetLastError()));
        }
    }

    // Call ShowOverlayOriginalMethod to ensure our overlay handles are explicitly shown
    // and our specific overlay tab is brought to the front in the dashboard.
    ShowOverlayOriginalMethod(); 

    // If all setup is successful, mark as continuously running.
    // This flag now mostly signifies that the rendering loop and video should be active.
    if(!continuousOverlayRunning) { // If it was false before this call (e.g. first time)
        continuousOverlayRunning = true;
        Log("ShowOverlayContinuous: Overlay operation loop is now active.");
    }
}

void HideOverlayContinuous()
{
    HWND mainWnd = FindWindowW(L"QuitVRAppClass", NULL);
    if (mainWnd) {
        KillTimer(mainWnd, OVERLAY_TIMER_ID);
    }

    // Stop the video thread with timeout
    g_stopVideoThread = true;
    if (g_videoThread.joinable()) {
        Log("HideOverlayContinuous -> Stopping video thread...");
        
        // Create a thread to monitor the join timeout
        std::thread safetyThread([]{
            // Wait 1 second max
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
            // If video thread is still joinable after timeout, detach it
            if (g_videoThread.joinable()) {
                Log("WARNING: Video thread join timed out - detaching to prevent deadlock");
                g_videoThread.detach();
            }
        });
        safetyThread.detach();
        
        // Try to join normally, but only wait for a short time
        auto joinStart = std::chrono::steady_clock::now();
        bool joined = false;
        
        while (std::chrono::steady_clock::now() - joinStart < std::chrono::milliseconds(1200)) {
            if (!g_videoThread.joinable()) {
                joined = true;
                break;
            }
            // Small sleep to avoid CPU spin
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        
        if (joined) {
            Log("HideOverlayContinuous -> Video thread joined successfully");
        } else {
            Log("HideOverlayContinuous -> Video thread join timed out, continuing anyway");
        }
    }

    // Clean up DirectX resources when hiding the overlay
    CleanupOverlayDirectX();

    // Only attempt to clean up OpenVR if it was initialized
    // Note: VR Shutdown should ideally happen only on app exit, not here.
    // If InitializeOpenVRForOverlay uses a static flag, this check is okay.
    bool openVRInitialized = InitializeOpenVRForOverlay(); // Check if it was ever initialized
    if (!openVRInitialized) {
        Log("HideOverlayContinuous -> VR not inited, skipping OpenVR cleanup.");
        if (continuousOverlayRunning) {
            Log("Marking continuous overlay as stopped (without OpenVR cleanup).");
            continuousOverlayRunning = false;
        }
        return;
    }

    vr::IVROverlay* pOverlay = vr::VROverlay();
    if (!pOverlay) {
        Log("HideOverlayContinuous -> pOverlay not available, skipping.");
        if (continuousOverlayRunning) {
            Log("Marking continuous overlay as stopped (without overlay cleanup).");
            continuousOverlayRunning = false;
        }
        return;
    }

    if (g_MainOverlay != 0) {
        vr::EVROverlayError err = pOverlay->DestroyOverlay(g_MainOverlay);
        if (err == vr::VROverlayError_None) {
            g_MainOverlay = 0;
            Log("Main overlay destroyed.");
        } else {
            Log("Failed to destroy main overlay? err=" + std::to_string((int)err));
        }
    }
    if (g_ThumbnailOverlay != 0) {
        vr::EVROverlayError err = pOverlay->DestroyOverlay(g_ThumbnailOverlay);
        if (err == vr::VROverlayError_None) {
            g_ThumbnailOverlay = 0;
            Log("Thumbnail overlay destroyed.");
        } else {
            Log("Failed to destroy thumbnail overlay? err=" + std::to_string((int)err));
        }
    }

    if (continuousOverlayRunning) {
        Log("Continuous overlay stopped.");
        continuousOverlayRunning = false;
    }
}

void StartContinuousOverlayTest()
{
    // Ensure SteamVR is running (don't launch it here, just check)
    if (!IsSteamVRRunning())
    {
        Log("StartContinuousOverlayTest -> SteamVR not running, waiting for persistent check.");
        return;
    }
    
    if (!InitializeOpenVRForOverlay())
    {
        Log("StartContinuousOverlayTest -> VR init fail.");
        return;
    }
    if (g_MainOverlay == 0)
    {
        PrepareOverlay();
    }
    ShowOverlayOriginalMethod();

    // Mark as running without setting a timer
    continuousOverlayRunning = true;
    Log("Overlay test shown once. No continuous refresh timer set.");
}

void StopContinuousOverlayTest()
{
    HideOverlayContinuous();
    Log("Continuous overlay test stopped.");
}

// New test function for video debugging
bool TestVideoPlayback(HWND hWnd)
{
    Log("TestVideoPlayback -> Starting video playback test...");
    
    // Initialize Media Foundation if needed
    HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        Log("TestVideoPlayback -> Failed to initialize Media Foundation. HRESULT: 0x" + std::to_string(hr));
        MessageBoxW(hWnd, L"Failed to initialize Media Foundation.", L"Video Test Error", MB_OK | MB_ICONERROR);
        return false;
    }
    
    // Check for video file
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    PathRemoveFileSpecW(exePath);
    std::wstring videoPath = std::wstring(exePath) + L"\\media\\logo-loop.mp4";
    
    Log("TestVideoPlayback -> Checking for video file: " + WStringToString(videoPath));
    
    DWORD attrs = GetFileAttributesW(videoPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        Log("TestVideoPlayback -> Video file not found!");
        MessageBoxW(hWnd, (L"Video file not found at:\n" + videoPath + L"\n\nPlease make sure you have a video file named logo-loop.mp4 in the media folder.").c_str(), 
                   L"Video Test Error", MB_OK | MB_ICONERROR);
        return false;
    }
    
    Log("TestVideoPlayback -> Video file found, creating source reader...");
    
    // Create source reader
    IMFSourceReader* pTestReader = nullptr;
    hr = MFCreateSourceReaderFromURL(videoPath.c_str(), NULL, &pTestReader);
    if (FAILED(hr)) {
        Log("TestVideoPlayback -> Failed to create Media Foundation source reader. HRESULT: 0x" + std::to_string(hr));
        MessageBoxW(hWnd, L"Failed to create Media Foundation source reader.", L"Video Test Error", MB_OK | MB_ICONERROR);
        return false;
    }
    
    Log("TestVideoPlayback -> Source reader created, trying to read metadata...");
    
    // Try to get native format information first
    IMFMediaType* pNativeType = NULL;
    hr = pTestReader->GetNativeMediaType(
        MF_SOURCE_READER_FIRST_VIDEO_STREAM,
        0,  // First available type
        &pNativeType
    );
    
    if (SUCCEEDED(hr) && pNativeType) {
        // Log native format info
        GUID majorType, subType;
        pNativeType->GetGUID(MF_MT_MAJOR_TYPE, &majorType);
        pNativeType->GetGUID(MF_MT_SUBTYPE, &subType);
        
        UINT32 width = 0, height = 0;
        MFGetAttributeSize(pNativeType, MF_MT_FRAME_SIZE, &width, &height);
        
        // Convert GUID to string for logging
        OLECHAR guidString[39];
        StringFromGUID2(subType, guidString, 39);
        std::wstring wSubtype(guidString);
        
        Log("TestVideoPlayback -> Native video format: " + WStringToString(wSubtype) + 
            ", Dimensions: " + std::to_string(width) + "x" + std::to_string(height));
            
        SAFE_RELEASE(pNativeType);
    }
    
    Log("TestVideoPlayback -> Trying to configure video format...");
    
    // Try multiple formats in order of preference
    const GUID formats[] = {
        MFVideoFormat_RGB32,    // First try RGB32 (our preferred format)
        MFVideoFormat_RGB24,    // Then RGB24
        MFVideoFormat_YUY2,     // Then YUY2 (more widely supported)
        MFVideoFormat_NV12      // Then NV12 (very common hardware format)
    };
    
    const char* formatNames[] = {
        "RGB32", "RGB24", "YUY2", "NV12"
    };
    
    bool formatConfigured = false;
    
    for (int i = 0; i < 4; i++) {
        Log("TestVideoPlayback -> Trying format: " + std::string(formatNames[i]));
        
        IMFMediaType* pMediaType = NULL;
        hr = MFCreateMediaType(&pMediaType);
        if (SUCCEEDED(hr)) {
            hr = pMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            if (SUCCEEDED(hr)) {
                hr = pMediaType->SetGUID(MF_MT_SUBTYPE, formats[i]);
                if (SUCCEEDED(hr)) {
                    hr = pTestReader->SetCurrentMediaType(
                        (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 
                        NULL, 
                        pMediaType);
                    
                    if (SUCCEEDED(hr)) {
                        Log("TestVideoPlayback -> Successfully configured format: " + std::string(formatNames[i]));
                        formatConfigured = true;
                        SAFE_RELEASE(pMediaType);
                        break;
                    } else {
                        Log("TestVideoPlayback -> Failed to set format " + std::string(formatNames[i]) + 
                            ". HRESULT: 0x" + std::to_string(hr));
                    }
                }
            }
            SAFE_RELEASE(pMediaType);
        }
    }
    
    if (!formatConfigured) {
        Log("TestVideoPlayback -> Failed to configure any video format. Last HRESULT: 0x" + std::to_string(hr));
        Log("TestVideoPlayback -> This could indicate missing codecs or an unsupported video format");
        
        // Check for MP4 codec availability
        CLSID clsidMPEG4Decoder = GUID_NULL;
        MFT_REGISTER_TYPE_INFO inputInfo = { MFMediaType_Video, MFVideoFormat_H264 };
        IMFActivate **ppActivate = NULL;
        UINT32 count = 0;
        
        hr = MFTEnumEx(
            MFT_CATEGORY_VIDEO_DECODER,
            MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER,
            &inputInfo,           // Input type
            NULL,                // Output type
            &ppActivate,
            &count
        );
        
        if (SUCCEEDED(hr) && count > 0) {
            Log("TestVideoPlayback -> Found " + std::to_string(count) + " potential H.264 decoders");
            for (UINT32 i = 0; i < count; i++) {
                ppActivate[i]->Release();
            }
            CoTaskMemFree(ppActivate);
        } else {
            Log("TestVideoPlayback -> No H.264 decoders found. HRESULT: 0x" + std::to_string(hr));
        }
        
        std::string errorMsg = "Failed to configure video format.\n\nThis usually means the video codec is not installed or the format is incompatible.\n\n";
        errorMsg += "Try re-encoding your video to H.264 format with standard settings.";
        
        MessageBoxA(hWnd, errorMsg.c_str(), "Video Format Error", MB_OK | MB_ICONERROR);
        SAFE_RELEASE(pTestReader);
        return false;
    }
    
    // Try to read some frames
    bool success = true;
    int frameCount = 0;
    bool hasEndOfStream = false;
    
    for (int i = 0; i < 10; i++) {
        DWORD streamIndex, flags;
        LONGLONG timestamp;
        IMFSample* pSample = NULL;
        
        hr = pTestReader->ReadSample(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            0,
            &streamIndex,
            &flags,
            &timestamp,
            &pSample
        );
        
        if (FAILED(hr)) {
            Log("TestVideoPlayback -> Failed to read sample #" + std::to_string(i) + 
                ". HRESULT: 0x" + std::to_string(hr));
            success = false;
            break;
        }
        
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            Log("TestVideoPlayback -> End of stream reached on frame #" + std::to_string(i));
            hasEndOfStream = true;
        }
        
        if (pSample) {
            frameCount++;
            SAFE_RELEASE(pSample);
        }
    }
    
    // Clean up
    SAFE_RELEASE(pTestReader);
    
    std::string resultMessage = "Video Test Results:\n\n";
    resultMessage += "Video file: " + WStringToString(videoPath) + "\n";
    resultMessage += "Frames successfully read: " + std::to_string(frameCount) + " out of 10\n";
    resultMessage += "End of stream reached: " + std::string(hasEndOfStream ? "Yes" : "No") + "\n\n";
    
    if (success && frameCount > 0) {
        resultMessage += "TEST PASSED: Video file can be read successfully.";
        Log("TestVideoPlayback -> PASSED: Successfully read " + std::to_string(frameCount) + " frames");
        MessageBoxA(hWnd, resultMessage.c_str(), "Video Test Success", MB_OK | MB_ICONINFORMATION);
        return true;
    } else {
        resultMessage += "TEST FAILED: Could not read video frames properly.";
        Log("TestVideoPlayback -> FAILED: Could only read " + std::to_string(frameCount) + " frames");
        MessageBoxA(hWnd, resultMessage.c_str(), "Video Test Failed", MB_OK | MB_ICONERROR);
        return false;
    }
}

// New function to reset video reader periodically
bool ResetVideoReader()
{
    if (!g_videoInitialized) // Don't check for g_pSourceReader, we want to re-create it if it's null
        return false;
        
    Log("Resetting video reader...");
    
    // Release current reader (this is safe if it's already null)
    SAFE_RELEASE(g_pSourceReader);
    
    // Create a new source reader with attributes for low latency
    HRESULT hr;
    
    // Create attributes for low latency mode
    IMFAttributes* pAttributes = NULL;
    hr = MFCreateAttributes(&pAttributes, 2);
    if (SUCCEEDED(hr))
    {
        // Set low-latency hint
        hr = pAttributes->SetUINT32(MF_LOW_LATENCY, TRUE);
        if (SUCCEEDED(hr))
        {
            Log("ResetVideoReader -> Set low latency attribute");
        }
        
        // Create source reader with attributes
        hr = MFCreateSourceReaderFromURL(g_videoPath.c_str(), pAttributes, &g_pSourceReader);
        SAFE_RELEASE(pAttributes);
    }
    else
    {
        // Fall back to creating source reader without attributes
        hr = MFCreateSourceReaderFromURL(g_videoPath.c_str(), NULL, &g_pSourceReader);
    }
    
    if (FAILED(hr))
    {
        Log("Failed to reset Media Foundation source reader. HRESULT: 0x" + std::to_string(hr));
        return false;
    }
    
    Log("ResetVideoReader -> Source reader created, examining native format...");
    
    // Try to get native format info first
    IMFMediaType* pNativeType = NULL;
    hr = g_pSourceReader->GetNativeMediaType(
        MF_SOURCE_READER_FIRST_VIDEO_STREAM,
        0,  // First available type
        &pNativeType
    );
    
    if (SUCCEEDED(hr) && pNativeType) {
        // Log native format info
        GUID majorType, subType;
        pNativeType->GetGUID(MF_MT_MAJOR_TYPE, &majorType);
        pNativeType->GetGUID(MF_MT_SUBTYPE, &subType);
        
        UINT32 width = 0, height = 0;
        MFGetAttributeSize(pNativeType, MF_MT_FRAME_SIZE, &width, &height);
        
        // Convert GUID to string for logging
        OLECHAR guidString[39];
        StringFromGUID2(subType, guidString, 39);
        std::wstring wSubtype(guidString);
        
        Log("ResetVideoReader -> Native video format: " + WStringToString(wSubtype) + 
            ", Dimensions: " + std::to_string(width) + "x" + std::to_string(height));
            
        SAFE_RELEASE(pNativeType);
    }
    
    Log("ResetVideoReader -> Reconfiguring video format...");
    
    // Try multiple formats in order of preference
    const GUID formats[] = {
        MFVideoFormat_RGB32,    // First try RGB32 (our preferred format)
        MFVideoFormat_RGB24,    // Then RGB24
        MFVideoFormat_YUY2,     // Then YUY2 (more widely supported)
        MFVideoFormat_NV12      // Then NV12 (very common hardware format)
    };
    
    const char* formatNames[] = {
        "RGB32", "RGB24", "YUY2", "NV12"
    };
    
    bool formatConfigured = false;
    
    for (int i = 0; i < 4; i++) {
        Log("ResetVideoReader -> Trying format: " + std::string(formatNames[i]));
        
        IMFMediaType* pMediaType = NULL;
        hr = MFCreateMediaType(&pMediaType);
        if (SUCCEEDED(hr)) {
            hr = pMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            if (SUCCEEDED(hr)) {
                hr = pMediaType->SetGUID(MF_MT_SUBTYPE, formats[i]);
                if (SUCCEEDED(hr)) {
                    hr = g_pSourceReader->SetCurrentMediaType(
                        (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 
                        NULL, 
                        pMediaType);
                    
                    if (SUCCEEDED(hr)) {
                        Log("ResetVideoReader -> Successfully configured format: " + std::string(formatNames[i]));
                        formatConfigured = true;
                        SAFE_RELEASE(pMediaType);
                        break;
                    } else {
                        Log("ResetVideoReader -> Failed to set format " + std::string(formatNames[i]) + 
                            ". HRESULT: 0x" + std::to_string(hr));
                    }
                }
            }
            SAFE_RELEASE(pMediaType);
        }
    }
    
    if (!formatConfigured) {
        Log("ResetVideoReader -> Failed to reconfigure any video format. HRESULT: 0x" + std::to_string(hr));
        return false;
    }
    
    // Request first sample to prime the pipeline
    DWORD streamIndex;
    DWORD flags;
    LONGLONG timestamp;
    IMFSample* pSample = NULL;
    
    g_pSourceReader->ReadSample(
        MF_SOURCE_READER_FIRST_VIDEO_STREAM,
        0,
        &streamIndex,
        &flags,
        &timestamp,
        &pSample
    );
    
    if (pSample)
    {
        SAFE_RELEASE(pSample);
    }
    
    Log("Video reader reset successful!");
    return true;
}

// Implementation of video processing thread
void VideoProcessingThread()
{
    Log("Video processing thread started");
    try {
        if (!InitializeVideo()) {
            Log("VideoProcessingThread -> Failed to initialize video");
            return;
        }
        int frameCounter = 0;
        while (!g_stopVideoThread)
        {
            if (g_stopVideoThread) {
                Log("VideoProcessingThread -> Stop flag detected at loop start");
                break;
            }
            auto startTime = std::chrono::high_resolution_clock::now();
            try {
                DWORD streamIndex, flags;
                LONGLONG timestamp;
                IMFSample* pSample = nullptr;
                HRESULT hr;
                // Removed periodic resets - they cause stuttering and are unnecessary
                // if (++frameCounter % 300 == 0) {
                //     Log("VideoProcessingThread -> Performing periodic video reader reset");
                //     if (!ResetVideoReader()) {
                //         Log("VideoProcessingThread -> Failed to reset video reader. Aborting thread.");
                //         g_stopVideoThread = true;
                //         continue; // Skip to next loop iteration, which will then exit
                //     }
                // }
                hr = g_pSourceReader->ReadSample(
                    MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                    0, &streamIndex, &flags, &timestamp, &pSample);
                if (FAILED(hr)) {
                    Log("VideoProcessingThread: ReadSample failed. HR=" + std::to_string(hr));
                    SAFE_RELEASE(pSample);
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }
                if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                    Log("VideoProcessingThread -> End of stream, seeking to beginning.");
                    PROPVARIANT var; PropVariantInit(&var);
                    var.vt = VT_I8; var.hVal.QuadPart = 0;
                    hr = g_pSourceReader->SetCurrentPosition(GUID_NULL, var);
                    PropVariantClear(&var);
                    SAFE_RELEASE(pSample);
                    if (FAILED(hr)) {
                        Log("VideoProcessingThread -> Failed to seek. HR=" + std::to_string(hr));
                        if (!ResetVideoReader()) {
                            Log("VideoProcessingThread -> Failed to reset video reader after seek failure. Aborting thread.");
                            g_stopVideoThread = true;
                            continue; // Skip to next loop iteration, which will then exit
                        }
                    }
                    continue;
                }
                if (!pSample) {
                    continue;
                }
                IMFMediaBuffer* pBuffer = nullptr;
                hr = pSample->ConvertToContiguousBuffer(&pBuffer);
                if (FAILED(hr)) {
                    Log("VideoProcessingThread: ConvertToContiguousBuffer failed. HR=" + std::to_string(hr));
                    SAFE_RELEASE(pBuffer); SAFE_RELEASE(pSample);
                    continue;
                }
                BYTE* pBytes = nullptr;
                DWORD bufferSize = 0;
                hr = pBuffer->Lock(&pBytes, NULL, &bufferSize);
                if (FAILED(hr)) {
                    Log("VideoProcessingThread: Lock failed. HR=" + std::to_string(hr));
                    SAFE_RELEASE(pBuffer); SAFE_RELEASE(pSample);
                    continue;
                }
                IMFMediaType* pCurrentMediaType = nullptr;
                UINT32 frameWidth = 0, frameHeight = 0;
                GUID currentSubtype = GUID_NULL;
                hr = g_pSourceReader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pCurrentMediaType);
                if (FAILED(hr) || !pCurrentMediaType) {
                    Log("VideoProcessingThread: GetCurrentMediaType failed. HR=" + std::to_string(hr));
                    pBuffer->Unlock(); SAFE_RELEASE(pBuffer); SAFE_RELEASE(pSample); SAFE_RELEASE(pCurrentMediaType);
                    continue;
                }
                MFGetAttributeSize(pCurrentMediaType, MF_MT_FRAME_SIZE, &frameWidth, &frameHeight);
                pCurrentMediaType->GetGUID(MF_MT_SUBTYPE, &currentSubtype);
                SAFE_RELEASE(pCurrentMediaType);
                if (frameWidth == 0 || frameHeight == 0) {
                    Log("VideoProcessingThread: Invalid frame dimensions.");
                    pBuffer->Unlock(); SAFE_RELEASE(pBuffer); SAFE_RELEASE(pSample);
                    continue;
                }
                std::vector<uint8_t> localRgbBuffer;
                bool conversionOk = false;
                if (currentSubtype == MFVideoFormat_RGB32 || currentSubtype == MFVideoFormat_ARGB32) {
                    localRgbBuffer.assign(pBytes, pBytes + bufferSize);
                    conversionOk = true;
                } else if (currentSubtype == MFVideoFormat_YUY2) {
                    localRgbBuffer.resize(frameWidth * frameHeight * 4);
                    for (UINT32 y = 0; y < frameHeight; ++y) {
                        for (UINT32 x = 0; x < frameWidth; x += 2) {
                            UINT32 yuyPos = y * (frameWidth * 2) + x * 2;
                            if (yuyPos + 3 >= bufferSize) continue;
                            BYTE Y0 = pBytes[yuyPos];     BYTE U  = pBytes[yuyPos + 1];
                            BYTE Y1 = pBytes[yuyPos + 2]; BYTE V  = pBytes[yuyPos + 3];
                            UINT32 rgbPos = y * (frameWidth * 4) + x * 4;
                            if (rgbPos + 7 >= localRgbBuffer.size()) continue;
                            int C0 = Y0 - 16; int C1 = Y1 - 16;
                            int D_val = U - 128; int E_val = V - 128;
                            localRgbBuffer[rgbPos + 0] = (BYTE)std::max(0, std::min(255, (int)(1.164f * C0 + 2.018f * D_val)));
                            localRgbBuffer[rgbPos + 1] = (BYTE)std::max(0, std::min(255, (int)(1.164f * C0 - 0.813f * E_val - 0.391f * D_val)));
                            localRgbBuffer[rgbPos + 2] = (BYTE)std::max(0, std::min(255, (int)(1.164f * C0 + 1.596f * E_val)));
                            localRgbBuffer[rgbPos + 3] = 255;
                            localRgbBuffer[rgbPos + 4] = (BYTE)std::max(0, std::min(255, (int)(1.164f * C1 + 2.018f * D_val)));
                            localRgbBuffer[rgbPos + 5] = (BYTE)std::max(0, std::min(255, (int)(1.164f * C1 - 0.813f * E_val - 0.391f * D_val)));
                            localRgbBuffer[rgbPos + 6] = (BYTE)std::max(0, std::min(255, (int)(1.164f * C1 + 1.596f * E_val)));
                            localRgbBuffer[rgbPos + 7] = 255;
                        }
                    }
                    conversionOk = true;
                }
                pBuffer->Unlock();
                if (conversionOk && !localRgbBuffer.empty()) {
                    std::lock_guard<std::mutex> lock(g_videoFrameMutex);
                    g_rawVideoFrameBuffer = std::move(localRgbBuffer);
                    g_rawVideoFrameWidth = frameWidth;
                    g_rawVideoFrameHeight = frameHeight;
                    g_rawVideoFrameReady = true;
                    g_videoFrameCV.notify_one();
                }
                SAFE_RELEASE(pBuffer);
                SAFE_RELEASE(pSample);
            }
            catch (const std::exception& e) {
                Log("VideoProcessingThread -> Exception during frame processing: " + std::string(e.what()));
                if (g_stopVideoThread) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
            // Calculate target frame time based on detected video frame rate
            int TARGET_FRAME_TIME_MS = static_cast<int>(1000.0f / g_videoFrameRate);
            if (duration.count() < TARGET_FRAME_TIME_MS && !g_stopVideoThread)
            {
                int sleepTime = TARGET_FRAME_TIME_MS - static_cast<int>(duration.count());
                // Use a single sleep for better timing precision
                std::this_thread::sleep_for(std::chrono::milliseconds(sleepTime));
            }
        }
    }
    catch (const std::exception& e) {
        Log("VideoProcessingThread -> Fatal exception: " + std::string(e.what()));
    }
    catch (...) {
        Log("VideoProcessingThread -> Unknown fatal exception");
    }
    Log("Video processing thread exiting");
    try {
        if (g_pSourceReader) {
            Log("VideoProcessingThread -> Releasing source reader");
            g_pSourceReader->Release();
            g_pSourceReader = nullptr;
        }
    }
    catch (...) {
        Log("VideoProcessingThread -> Exception during thread cleanup");
    }
}

// Handle mouse input for overlay game launcher
void HandleOverlayMouseEvent(const vr::VREvent_t& event)
{
    if (!g_SessionRunning) return;

    // If the mouse button is released, clear any held state flags.
    if (event.eventType == vr::VREvent_MouseButtonUp) {
        if (g_isGameListUpButtonHeld || g_isGameListDownButtonHeld) {
            g_isGameListUpButtonHeld = false;
            g_isGameListDownButtonHeld = false;
            g_timeGameListButtonHeld = 0;
            g_timeOfLastAutoScroll = 0;
        }
        return; // Stop processing for mouse up events.
    }

    // The rest of the function handles MouseButtonDown events.
    if (event.eventType != vr::VREvent_MouseButtonDown) return;

    float x = event.data.mouse.x;
    float y = event.data.mouse.y;

    // UNIVERSAL FIX: The user's experience shows the Y-axis is inverted across the entire overlay.
    // Flipping it here corrects all subsequent coordinate checks.
    y = OVERLAY_HEIGHT - y; 

    Log("Mouse click (Y-flipped): x=" + std::to_string(x) + ", y=" + std::to_string(y));
    
    // Basic coordinate logging (reduced spam)
    Log("Click in overlay area - checking UI zones...");

    // --- CATEGORY LIST HANDLING ---
    size_t numTotalCategories = g_categories.size();

    // Category scroll up button
    if (x >= g_rectCatScrollUpButton.left && x <= g_rectCatScrollUpButton.right &&
        y >= g_rectCatScrollUpButton.top && y <= g_rectCatScrollUpButton.bottom) {
        Log("Category scroll up button clicked.");
        if (g_categoryListScrollOffset > 0) {
            g_categoryListScrollOffset--;
        }
        return;
    }
    // Category scroll down button
    if (x >= g_rectCatScrollDownButton.left && x <= g_rectCatScrollDownButton.right &&
        y >= g_rectCatScrollDownButton.top && y <= g_rectCatScrollDownButton.bottom) {
        Log("Category scroll down button clicked.");
        // Allow scrolling as long as we aren't at the very end. The drawing logic will prevent overscrolling visually.
        if (static_cast<size_t>(g_categoryListScrollOffset) < numTotalCategories) {
             g_categoryListScrollOffset++;
        }
        return;
    }

    // Accurate category item click detection
    if (x >= g_rectCategoryList.left && x <= g_rectCategoryList.right &&
        y >= g_rectCategoryList.top && y <= g_rectCategoryList.bottom) {
        
        Log(">>> HIT: Category list area clicked. Checking items with dynamic height...");

        float currentY = g_rectCategoryList.top;
        float listTextPadding = 10.0f;
        float interItemSpacing = 8.0f;

        for (int i = 0; ; ++i) {
            int actualCatIndex = g_categoryListScrollOffset + i;
            if (actualCatIndex < 0 || static_cast<size_t>(actualCatIndex) >= numTotalCategories) {
                break; // No more categories to check
            }
            if (!g_categories[actualCatIndex]) continue;

            // Calculate item height exactly as in the drawing function
            std::wstring wCategoryName = StringToWString(g_categories[actualCatIndex]->name);
            if (wCategoryName.empty()) wCategoryName = L"(Unnamed Category)";

            float textLayoutBoxWidth = g_rectCategoryList.right - g_rectCategoryList.left - (listTextPadding * 2);
            if (textLayoutBoxWidth <= 0) textLayoutBoxWidth = 1.0f;

            IDWriteTextLayout* pCategoryNameTextLayout = nullptr;
            HRESULT hr_layout = g_pDWriteFactory->CreateTextLayout(
                wCategoryName.c_str(), wCategoryName.length(), g_pTextFormatCategoryList,
                textLayoutBoxWidth, g_rectCategoryList.bottom, // Max height
                &pCategoryNameTextLayout
            );

            float currentItemDisplayHeight = 35.0f; // Fallback height
            if (SUCCEEDED(hr_layout) && pCategoryNameTextLayout) {
                DWRITE_TEXT_METRICS textMetrics;
                pCategoryNameTextLayout->GetMetrics(&textMetrics);
                currentItemDisplayHeight = textMetrics.height;
                SAFE_RELEASE(pCategoryNameTextLayout);
            }

            // Define the bounding box for this specific item
            D2D1_RECT_F itemBound = D2D1::RectF(
                g_rectCategoryList.left,
                currentY,
                g_rectCategoryList.right,
                currentY + currentItemDisplayHeight
            );

            // Check if the click is within this item's calculated bounds
            if (y >= itemBound.top && y <= itemBound.bottom) {
                Log("ACCURATE HIT: Category '" + g_categories[actualCatIndex]->name + "' at index " + std::to_string(actualCatIndex));
                if (actualCatIndex != g_selectedCategoryIndexVR) {
                    g_selectedCategoryIndexVR = actualCatIndex;
                    UpdateCurrentCategoryGameList_VR();

                    // --- NEW LOGIC TO AUTO-SELECT FIRST GAME ---
                    std::string appIdForInitialVRLoad = "";
                    HWND hwndMainForSim = FindWindowW(L"QuitVRAppClass", NULL);

                    { // Scope for VR list and overlay state mutexes
                        std::lock_guard<std::mutex> vrListLock(g_VROverlayGameListMutex);
                        std::lock_guard<std::mutex> overlayLock(g_overlayStateMutex);

                        if (!g_currentCategoryGameAppIds_VR.empty()) {
                            g_selectedGameIndex = 0; // Select first game in the new category
                            appIdForInitialVRLoad = g_currentCategoryGameAppIds_VR[0];
                            g_isLoadingGameData = true;
                            SAFE_RELEASE(g_pSelectedGameHeader);
                            SAFE_RELEASE(g_pDescTextLayout);
                            g_descScrollOffsetPx = 0;
                            g_currentLayoutAppId = "";
                            Log("HandleOverlayMouseEvent: Auto-selecting first game for VR overlay: AppID " + appIdForInitialVRLoad);
                        } else {
                            g_selectedGameIndex = -1;
                            g_isLoadingGameData = false;
                            // Clear existing game details if category is empty
                            SAFE_RELEASE(g_pSelectedGameHeader);
                            SAFE_RELEASE(g_pDescTextLayout);
                            g_descScrollOffsetPx = 0;
                            g_currentLayoutAppId = "";
                            Log("HandleOverlayMouseEvent: New category is empty. Clearing VR game details.");
                        }
                    } // Mutexes released

                    if (!appIdForInitialVRLoad.empty() && hwndMainForSim) {
                        std::thread([appId = appIdForInitialVRLoad, hwndMain = hwndMainForSim]() {
                            Log("HandleOverlayMouseEvent (Thread): Starting data fetch for AppID " + appId);
                            GameDataResult* result = new GameDataResult();
                            result->appId = appId;
                            result->success = false;

                            SteamGame* gamePtr = findGameByAppId(appId);
                            if (gamePtr) {
                                FetchStoreDataForGame(*gamePtr);

                                std::lock_guard<std::mutex> gameDataLock(gamePtr->dataMutex);
                                result->description = gamePtr->description;
                                result->headerUrl = gamePtr->headerImage;
                                result->success = gamePtr->storeDataFetched;

                                if (result->success && !result->headerUrl.empty()) {
                                    FixEscapedSlashes(result->headerUrl);
                                    std::string imageCachePathToUse;
                                    bool isManualCacheMarker = (result->headerUrl.rfind("cache://", 0) == 0);

                                    if (isManualCacheMarker) {
                                        imageCachePathToUse = GetCachePathForImage(appId, result->headerUrl.substr(8));
                                    } else {
                                        imageCachePathToUse = GetCachePathForImage(appId, result->headerUrl);
                                    }

                                    if (LoadImageBytesFromCache(imageCachePathToUse, result->imageBytes)) {
                                        Log("HandleOverlayMouseEvent (Thread): Image bytes for " + gamePtr->name + " loaded from cache.");
                                    } else if (!isManualCacheMarker) {
                                        Log("HandleOverlayMouseEvent (Thread): Image for " + gamePtr->name + " not in cache, download from: " + result->headerUrl);
                                        std::wstring wImageUrl = StringToWString(result->headerUrl);
                                        URL_COMPONENTSW urlComp = {0}; urlComp.dwStructSize = sizeof(urlComp);
                                        const DWORD buffSize = 1024;
                                        wchar_t szScheme[buffSize], szHostName[buffSize], szUrlPath[buffSize], szExtraInfo[buffSize];
                                        urlComp.lpszScheme = szScheme; urlComp.dwSchemeLength = buffSize;
                                        urlComp.lpszHostName = szHostName; urlComp.dwHostNameLength = buffSize;
                                        urlComp.lpszUrlPath = szUrlPath; urlComp.dwUrlPathLength = buffSize;
                                        urlComp.lpszExtraInfo = szExtraInfo; urlComp.dwExtraInfoLength = buffSize;

                                        if (WinHttpCrackUrl(wImageUrl.c_str(), (DWORD)wImageUrl.length(), 0, &urlComp)) {
                                            bool bSecure = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);
                                            std::wstring server(urlComp.lpszHostName, urlComp.dwHostNameLength);
                                            std::wstring path_ws = std::wstring(urlComp.lpszUrlPath, urlComp.dwUrlPathLength) + std::wstring(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
                                            result->imageBytes = HttpDownloadToVector(server, path_ws, bSecure);
                                            if (!result->imageBytes.empty()) {
                                                Log("HandleOverlayMouseEvent (Thread): Image for " + gamePtr->name + " downloaded.");
                                                SaveImageBytesToCache(imageCachePathToUse, result->imageBytes);
                                            } else {
                                                Log("HandleOverlayMouseEvent (Thread): Failed to download image for " + gamePtr->name + ".");
                                            }
                                        } else {
                                            Log("HandleOverlayMouseEvent (Thread): Failed to crack image URL for " + gamePtr->name + ".");
                                        }
                                    }
                                }
                            } else {
                                Log("HandleOverlayMouseEvent (Thread): Game with AppID " + appId + " not found.");
                                result->success = false;
                            }
                            PostMessage(hwndMain, WM_APP_GAME_DATA_READY, 0, (LPARAM)result);
                        }).detach();
                    }
                     // --- END OF NEW LOGIC ---
                }
                return; // Click handled
            }

            // Advance Y for the next item's bounds check
            currentY += currentItemDisplayHeight + interItemSpacing;
            if (currentY >= g_rectCategoryList.bottom) {
                break; // Past visible area
            }
        }
        return; // Click was in the list area but not on a specific item
    }

    // --- GAME LIST HANDLING (filtered by selected category) ---
    float itemHeight = 40.0f;
    int maxVisibleGames = (itemHeight > 0) ? static_cast<int>((g_rectGameList.bottom - g_rectGameList.top) / itemHeight) : 0;
    size_t numGames = 0;
    std::vector<std::string> gamesToShowInClickHandle;
    {
        std::lock_guard<std::mutex> vr_list_lock(g_VROverlayGameListMutex);
        gamesToShowInClickHandle = g_currentCategoryGameAppIds_VR;
        numGames = gamesToShowInClickHandle.size();
    }
    // Game list scroll up button
    if (x >= g_rectScrollUpButton.left && x <= g_rectScrollUpButton.right &&
        y >= g_rectScrollUpButton.top && y <= g_rectScrollUpButton.bottom) {
        Log("Up scroll button clicked.");

        // Set state for button hold
        g_isGameListUpButtonHeld = true;
        g_isGameListDownButtonHeld = false;
        g_timeGameListButtonHeld = GetTickCount();
        g_timeOfLastAutoScroll = g_timeGameListButtonHeld; // Initialize last scroll time

        // Perform single scroll action
        if (g_gameListScrollOffset > 0) {
            g_gameListScrollOffset--;
        }
        return;
    }
    // Game list scroll down button
    if (x >= g_rectScrollDownButton.left && x <= g_rectScrollDownButton.right &&
        y >= g_rectScrollDownButton.top && y <= g_rectScrollDownButton.bottom) {

        // Set state for button hold
        g_isGameListDownButtonHeld = true;
        g_isGameListUpButtonHeld = false;
        g_timeGameListButtonHeld = GetTickCount();
        g_timeOfLastAutoScroll = g_timeGameListButtonHeld; // Initialize last scroll time

        // Perform single scroll action
        if (g_gameListScrollOffset + g_lastVisibleGameCount < numGames) {
             Log("Down scroll button clicked. Scrolling down.");
             g_gameListScrollOffset++;
        } else {
             Log("Down scroll button clicked, but at the end of the list. No action taken.");
        }
        return;
    }
    // Game item click (only if category is selected) - ACCURATE CLICK DETECTION
    if (g_selectedCategoryIndexVR != -1 &&
        x >= g_rectGameList.left && x <= g_rectGameList.right &&
        y >= g_rectGameList.top && y <= g_rectGameList.bottom) {

        Log(">>> HIT: Game list area clicked. numGames = " + std::to_string(numGames) + ", g_gameListScrollOffset = " + std::to_string(g_gameListScrollOffset));

        if (numGames == 0) {
            Log("HandleOverlayMouseEvent: No games in category to click.");
            return;
        }
        
        // ACCURATE CLICK DETECTION: Re-implementing iterative height calculation
        // to match the drawing logic exactly. This fixes cumulative offsets from variable-height items (e.g., text wrapping).
        float currentY = g_rectGameList.top;
        float listTextPadding = 10.0f;
        float interItemSpacing = 8.0f;

        for (int i = 0; ; ++i) {
            int gameListActualIndex = g_gameListScrollOffset + i;
            if (gameListActualIndex < 0 || static_cast<size_t>(gameListActualIndex) >= numGames) {
                break; // No more games
            }

            // Get game info without excessive lookups
            const std::string& gameAppId = gamesToShowInClickHandle[gameListActualIndex];
            SteamGame* game = findGameByAppId(gameAppId); // This is fast now
            if (!game) continue;

            // Calculate this item's height exactly as the drawing code does
            std::wstring wGameName = StringToWString(game->name);
            if (wGameName.empty()) wGameName = L"(Unnamed Game)";
            
            float textLayoutBoxWidth = g_rectGameList.right - g_rectGameList.left - (listTextPadding * 2);
            if (textLayoutBoxWidth <= 0) textLayoutBoxWidth = 1.0f;

            IDWriteTextLayout* pGameNameTextLayout = nullptr;
            HRESULT hr_layout = g_pDWriteFactory->CreateTextLayout(
                wGameName.c_str(), wGameName.length(), g_pTextFormatGameList,
                textLayoutBoxWidth, g_rectGameList.bottom, // Give it max possible height
                &pGameNameTextLayout
            );

            float currentItemDisplayHeight = 30.0f; // Fallback height
            if (SUCCEEDED(hr_layout) && pGameNameTextLayout) {
                DWRITE_TEXT_METRICS textMetrics;
                pGameNameTextLayout->GetMetrics(&textMetrics);
                currentItemDisplayHeight = textMetrics.height;
                SAFE_RELEASE(pGameNameTextLayout);
            }

            D2D1_RECT_F itemBound = D2D1::RectF(
                g_rectGameList.left,
                currentY,
                g_rectGameList.right,
                currentY + currentItemDisplayHeight
            );

            // Check if the universally-flipped 'y' is within this item's calculated bounds
            if (y >= itemBound.top && y <= itemBound.bottom) {
                Log("ACCURATE HIT: Game '" + game->name + "' (AppID: " + game->appid + ") at index " + std::to_string(gameListActualIndex));
                
                bool shouldLoadData = false;
                { // Scope for overlayStateMutex
                    std::lock_guard<std::mutex> overlayLock(g_overlayStateMutex);
                    if (g_selectedGameIndex != gameListActualIndex || !g_isLoadingGameData) {
                        g_launchingAppId = ""; // Reset launching state on new selection
                        g_selectedGameIndex = gameListActualIndex;
                        g_isLoadingGameData = true;
                        SAFE_RELEASE(g_pSelectedGameHeader);
                        SAFE_RELEASE(g_pDescTextLayout);
                        g_descScrollOffsetPx = 0;
                        g_currentLayoutAppId = "";
                        shouldLoadData = true;
                        Log("HandleOverlayMouseEvent: Game selected. AppID: " + game->appid + ". Set isLoadingData=true.");
                    } else {
                        Log("HandleOverlayMouseEvent: Game item " + game->appid + " already selected/loading. Ignoring redundant click.");
                    }
                } // overlayStateMutex released

                if (shouldLoadData) {
                    std::thread([appIdToLoad = game->appid, mainHwnd = FindWindowW(L"QuitVRAppClass", NULL)]() {
                        Log("FAST LOAD: Starting data fetch for AppID " + appIdToLoad);
                        GameDataResult* result = new GameDataResult();
                        result->appId = appIdToLoad;
                        result->success = false;
                        SteamGame* gamePtr = findGameByAppId(appIdToLoad); // Use captured appIdToLoad
                        if (gamePtr) {
                            FetchStoreDataForGame(*gamePtr); 
                            std::lock_guard<std::mutex> gameDataLock(gamePtr->dataMutex);
                            result->description = gamePtr->description;
                            result->headerUrl = gamePtr->headerImage;
                            if (!gamePtr->headerImage.empty()) {
                                FixEscapedSlashes(result->headerUrl); 
                                std::string imageCachePathToUse;
                                bool isManualCacheMarker = (result->headerUrl.rfind("cache://", 0) == 0);
                                if (isManualCacheMarker) {
                                    std::string cacheFilename = result->headerUrl.substr(8);
                                    imageCachePathToUse = GetCachePathForImage(appIdToLoad, cacheFilename); 
                                } else {
                                    imageCachePathToUse = GetCachePathForImage(appIdToLoad, result->headerUrl);
                                }
                                bool loadedFromImgCache = LoadImageBytesFromCache(imageCachePathToUse, result->imageBytes); 
                                if (!loadedFromImgCache && !isManualCacheMarker) {
                                    std::wstring wImageUrl = StringToWString(result->headerUrl);
                                    URL_COMPONENTSW urlComp = {0};
                                    urlComp.dwStructSize = sizeof(urlComp);
                                    const DWORD buffSize = 1024;
                                    wchar_t szScheme[buffSize], szHostName[buffSize], szUrlPath[buffSize], szExtraInfo[buffSize];
                                    urlComp.lpszScheme = szScheme; urlComp.dwSchemeLength = buffSize;
                                    urlComp.lpszHostName = szHostName; urlComp.dwHostNameLength = buffSize;
                                    urlComp.lpszUrlPath = szUrlPath; urlComp.dwUrlPathLength = buffSize;
                                    urlComp.lpszExtraInfo = szExtraInfo; urlComp.dwExtraInfoLength = buffSize;
                                    if (WinHttpCrackUrl(wImageUrl.c_str(), static_cast<UINT32>(wImageUrl.length()), 0, &urlComp)) {
                                        bool bSecure = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);
                                        std::wstring server(urlComp.lpszHostName, urlComp.dwHostNameLength);
                                        std::wstring path_ws = std::wstring(urlComp.lpszUrlPath, urlComp.dwUrlPathLength) +
                                                           std::wstring(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
                                        result->imageBytes = HttpDownloadToVector(server, path_ws, bSecure); 
                                        if (!result->imageBytes.empty()) {
                                            SaveImageBytesToCache(imageCachePathToUse, result->imageBytes); 
                                        }
                                    }
                                }
                                result->success = !result->imageBytes.empty() || (gamePtr->storeDataFetched && result->headerUrl.empty());
                            } else {
                                result->success = gamePtr->storeDataFetched;
                            }
                        } else {
                             result->success = false;
                             Log("Async load: Game with AppID " + appIdToLoad + " not found.");
                        }
                        if (mainHwnd) {
                            PostMessage(mainHwnd, WM_APP_GAME_DATA_READY, 0, (LPARAM)result);
                        } else {
                             Log("Async load: Main window handle null. Deleting result for AppID " + appIdToLoad);
                             delete result; 
                        }
                    }).detach();
                }
                return; // Click handled
            }

            currentY += currentItemDisplayHeight + interItemSpacing;
            if (currentY >= g_rectGameList.bottom) {
                break; // Past visible area
            }
        }
    }
    // --- End of Game item click ---

    // Check if click is in launch button
    else if (x >= g_rectStartButton.left && x <= g_rectStartButton.right && y >= g_rectStartButton.top && y <= g_rectStartButton.bottom) {
        Log("Start Game button clicked in VR overlay.");
        std::string appIdToLaunch;
        bool gameSelectedForLaunch = false;

        { // Scope for mutexes
            std::lock_guard<std::mutex> overlayLock(g_overlayStateMutex); // For g_selectedGameIndex
            std::lock_guard<std::mutex> vrListLock(g_VROverlayGameListMutex); // For g_currentCategoryGameAppIds_VR

            if (g_selectedGameIndex >= 0 && 
                static_cast<size_t>(g_selectedGameIndex) < g_currentCategoryGameAppIds_VR.size()) {
                appIdToLaunch = g_currentCategoryGameAppIds_VR[g_selectedGameIndex];
                gameSelectedForLaunch = true;
            }
        } // Mutexes released

        if (gameSelectedForLaunch && !appIdToLaunch.empty()) {
            SteamGame* gameToLaunch = findGameByAppId(appIdToLaunch); // findGameByAppId handles its own g_gamesMutex
            if (gameToLaunch) {
                Log("Launching game via Start Button: " + gameToLaunch->name + " (AppID: " + appIdToLaunch + ")");
                LaunchGame(*gameToLaunch);
            } else {
                Log("HandleOverlayMouseEvent: Start button clicked, but game with AppID " + appIdToLaunch + " not found in master list.");
            }
        } else {
            Log("HandleOverlayMouseEvent: Start button clicked, but no game (or invalid index) selected in VR overlay's current list.");
            // Optionally, show a message to the user in VR if possible, or just log.
        }
        return; 
    }
    // Check if click is in Quit Game button
    else if (x >= g_rectQuitButton.left && x <= g_rectQuitButton.right && y >= g_rectQuitButton.top && y <= g_rectQuitButton.bottom) {
        Log("Quit Game button clicked in VR overlay.");
        QuitVRApp(); // Call the existing function to quit the VR application
        return;
    }
}

// New function to handle continuous scrolling when a button is held
void UpdateContinuousScroll()
{
    // Check if a scroll button is being held down
    if (!g_isGameListUpButtonHeld && !g_isGameListDownButtonHeld) {
        return; // Nothing to do
    }

    // Check if the initial hold delay has passed (e.g., 400ms)
    const DWORD HOLD_DELAY_MS = 400;
    if (GetTickCount() - g_timeGameListButtonHeld < HOLD_DELAY_MS) {
        return; // Still in the initial delay period
    }

    // Check if enough time has passed since the last auto-scroll (e.g., 120ms)
    const DWORD AUTO_SCROLL_INTERVAL_MS = 120;
    if (GetTickCount() - g_timeOfLastAutoScroll < AUTO_SCROLL_INTERVAL_MS) {
        return; // Not time for the next scroll yet
    }

    // It's time to perform a scroll.
    g_timeOfLastAutoScroll = GetTickCount(); // Update the timestamp for this scroll

    // Determine which direction to scroll
    if (g_isGameListUpButtonHeld) {
        if (g_gameListScrollOffset > 0) {
            g_gameListScrollOffset--;
        } else {
            // At the top, stop holding.
            g_isGameListUpButtonHeld = false;
            g_timeGameListButtonHeld = 0;
        }
    } else if (g_isGameListDownButtonHeld) {
        size_t numGames = 0;
        {
            std::lock_guard<std::mutex> vr_list_lock(g_VROverlayGameListMutex);
            numGames = g_currentCategoryGameAppIds_VR.size();
        }

        // Use g_lastVisibleGameCount to ensure we don't scroll past the end
        if (g_gameListScrollOffset + g_lastVisibleGameCount < numGames) {
             g_gameListScrollOffset++;
        } else {
            // At the bottom, stop holding.
            g_isGameListDownButtonHeld = false;
            g_timeGameListButtonHeld = 0;
        }
    }
}

// Add this function to poll overlay events and handle mouse input
void PollOverlayEvents()
{
    if (!g_MainOverlay) return;
    vr::VREvent_t event;
    while (vr::VROverlay()->PollNextOverlayEvent(g_MainOverlay, &event, sizeof(event))) {
        if (event.eventType == vr::VREvent_MouseButtonDown || event.eventType == vr::VREvent_MouseButtonUp) { // Handle both down and up events
            HandleOverlayMouseEvent(event);
        }
        // Ignore VREvent_MouseMove events to prevent controller vibration
    }
}

// Function to load an image from a URL *or cache marker* using WinHTTP and WIC into a D2D Bitmap
bool LoadImageFromURLToD2DBitmap(const std::string& appid, const std::string& imageLocation, ID2D1Bitmap** ppBitmap) {
    if (!ppBitmap || !g_pWICFactory || !g_pD2DContext) {
        Log("LoadImageFromURLToD2DBitmap: Invalid arguments or DirectX/WIC not ready.");
        if (ppBitmap) *ppBitmap = nullptr;
        return false;
    }
    *ppBitmap = nullptr; // Initialize output parameter

    if (appid.empty() || imageLocation.empty()) {
        Log("LoadImageFromURLToD2DBitmap: Empty appid or imageLocation provided.");
        return false;
    }

    Log("LoadImageFromURLToD2DBitmap: Attempting to load image for appid " + appid + " from location: [" + imageLocation + "]");

    std::vector<uint8_t> imageBytes;
    bool loadedFromCache = false;
    bool isCacheMarker = (imageLocation.rfind("cache://", 0) == 0);
    std::string imageCachePath = "";

    if (isCacheMarker) {
        std::string cacheFilename = imageLocation.substr(8);
        if (cacheFilename.empty()) {
            Log("LoadImageFromURLToD2DBitmap: Invalid cache marker format: " + imageLocation);
            return false;
        }
        imageCachePath = GetCachePathForImage(appid, cacheFilename);
        Log("LoadImageFromURLToD2DBitmap: Cache marker detected. Attempting to load directly from: " + imageCachePath);
        if (LoadImageBytesFromCache(imageCachePath, imageBytes) && !imageBytes.empty()) {
            Log("LoadImageFromURLToD2DBitmap: Successfully loaded image bytes from specified cache path.");
            loadedFromCache = true;
        } else {
            Log("LoadImageFromURLToD2DBitmap: Failed to load image from cache path specified by marker: " + imageCachePath);
            return false;
        }
    } else {
        imageCachePath = GetCachePathForImage(appid, imageLocation);
        if (LoadImageBytesFromCache(imageCachePath, imageBytes) && !imageBytes.empty()) {
            Log("LoadImageFromURLToD2DBitmap: Found valid image in standard cache: " + imageCachePath);
            loadedFromCache = true;
        } else {
            Log("LoadImageFromURLToD2DBitmap: Image not in standard cache: " + imageCachePath);
        }
    }

    if (!isCacheMarker && !loadedFromCache) {
        Log("LoadImageFromURLToD2DBitmap: Downloading image from URL: " + imageLocation);
        std::wstring wImageUrl = StringToWString(imageLocation);
        URL_COMPONENTSW urlComp = {0};
        urlComp.dwStructSize = sizeof(urlComp);
        const DWORD buffSize = 1024;
        wchar_t szScheme[buffSize], szHostName[buffSize], szUrlPath[buffSize], szExtraInfo[buffSize];
        urlComp.lpszScheme = szScheme; urlComp.dwSchemeLength = buffSize;
        urlComp.lpszHostName = szHostName; urlComp.dwHostNameLength = buffSize;
        urlComp.lpszUrlPath = szUrlPath; urlComp.dwUrlPathLength = buffSize;
        urlComp.lpszExtraInfo = szExtraInfo; urlComp.dwExtraInfoLength = buffSize;

        if (!WinHttpCrackUrl(wImageUrl.c_str(), static_cast<UINT32>(wImageUrl.length()), 0, &urlComp)) {
            Log("LoadImageFromURLToD2DBitmap: WinHttpCrackUrl failed. Error: " + std::to_string(GetLastError()) + ". URL: " + WStringToString(wImageUrl));
            return false;
        }
        bool bSecure = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);
        std::wstring server(urlComp.lpszHostName, urlComp.dwHostNameLength);
        std::wstring path = std::wstring(urlComp.lpszUrlPath, urlComp.dwUrlPathLength) +
                            std::wstring(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
        imageBytes = HttpDownloadToVector(server, path, bSecure);
        if (imageBytes.empty()) {
            Log("LoadImageFromURLToD2DBitmap: Failed to download image data or received empty response from: " + imageLocation);
            return false;
        }
        Log("LoadImageFromURLToD2DBitmap: Downloaded " + std::to_string(imageBytes.size()) + " bytes.");
        if (!SaveImageBytesToCache(imageCachePath, imageBytes)) {
            Log("LoadImageFromURLToD2DBitmap: WARNING - Failed to save downloaded image to cache: " + imageCachePath);
        }
    }

    HRESULT hr = S_OK;
    IWICImagingFactory* pWICFactory = g_pWICFactory;
    if (!pWICFactory) {
        Log("LoadImageFromURLToD2DBitmap: WIC Factory is null!");
        return false;
    }
    IWICStream* pIWICStream = nullptr;
    IWICBitmapDecoder* pIDecoder = nullptr;
    IWICBitmapFrameDecode* pIDecoderFrame = nullptr;
    IWICFormatConverter* pIFormatConverter = nullptr;
    hr = pWICFactory->CreateStream(&pIWICStream);
    if (SUCCEEDED(hr)) {
        hr = pIWICStream->InitializeFromMemory(imageBytes.data(), (DWORD)imageBytes.size());
    } else {
        Log("LoadImageFromURLToD2DBitmap: Failed CreateStream. HR=" + std::to_string(hr));
    }
    if (SUCCEEDED(hr)) {
        hr = pWICFactory->CreateDecoderFromStream(pIWICStream, NULL, WICDecodeMetadataCacheOnDemand, &pIDecoder);
    } else {
        Log("LoadImageFromURLToD2DBitmap: Failed CreateDecoderFromStream. HR=" + std::to_string(hr));
    }
    if (SUCCEEDED(hr)) {
        hr = pIDecoder->GetFrame(0, &pIDecoderFrame);
    } else {
        Log("LoadImageFromURLToD2DBitmap: Failed GetFrame. HR=" + std::to_string(hr));
    }
    if (SUCCEEDED(hr)) {
        hr = pWICFactory->CreateFormatConverter(&pIFormatConverter);
    } else {
        Log("LoadImageFromURLToD2DBitmap: Failed CreateFormatConverter. HR=" + std::to_string(hr));
    }
    if (SUCCEEDED(hr)) {
        hr = pIFormatConverter->Initialize(pIDecoderFrame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, NULL, 0.f, WICBitmapPaletteTypeCustom);
        if (FAILED(hr)) {
            Log("LoadImageFromURLToD2DBitmap: Format Converter Initialize Failed! HR=" + std::to_string(hr));
        }
    }
    if (SUCCEEDED(hr)) {
        hr = g_pD2DContext->CreateBitmapFromWicBitmap(pIFormatConverter, NULL, ppBitmap);
        if (FAILED(hr)) {
            Log("LoadImageFromURLToD2DBitmap: CreateBitmapFromWicBitmap Failed! HR=" + std::to_string(hr));
        }
    } else {
        Log("LoadImageFromURLToD2DBitmap: Skipping CreateBitmapFromWicBitmap due to previous errors.");
    }
    SAFE_RELEASE(pIWICStream);
    SAFE_RELEASE(pIDecoder);
    SAFE_RELEASE(pIDecoderFrame);
    SAFE_RELEASE(pIFormatConverter);
    if (SUCCEEDED(hr) && *ppBitmap) {
        Log("LoadImageFromURLToD2DBitmap: Successfully loaded/decoded and created D2D bitmap for " + imageLocation);
        return true;
    } else {
        Log("LoadImageFromURLToD2DBitmap: Failed to create final D2D bitmap for " + imageLocation + ". Final HR=" + std::to_string(hr));
        SAFE_RELEASE(*ppBitmap);
        if (loadedFromCache) {
            Log("Deleting potentially corrupt cache file: " + imageCachePath);
            DeleteFileA(imageCachePath.c_str());
        }
        return false;
    }
}

bool LoadImageFromFileToD2DBitmap(const std::wstring& filePath, ID2D1Bitmap** ppBitmap) {
    if (!ppBitmap || !g_pWICFactory || !g_pD2DContext) {
        Log("LoadImageFromFileToD2DBitmap: Invalid arguments or DirectX/WIC not ready.");
        if (ppBitmap) *ppBitmap = nullptr;
        return false;
    }
    *ppBitmap = nullptr;

    IWICBitmapDecoder *pDecoder = nullptr;
    HRESULT hr = g_pWICFactory->CreateDecoderFromFilename(
        filePath.c_str(),
        NULL,
        GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,
        &pDecoder
    );

    if (FAILED(hr)) {
        Log("LoadImageFromFileToD2DBitmap: Failed to create WIC decoder for " + WStringToString(filePath) + ". HR=" + std::to_string(hr));
        return false;
    }

    IWICBitmapFrameDecode *pFrame = nullptr;
    if (SUCCEEDED(hr)) {
        hr = pDecoder->GetFrame(0, &pFrame);
    }

    IWICFormatConverter *pConverter = nullptr;
    if (SUCCEEDED(hr)) {
        hr = g_pWICFactory->CreateFormatConverter(&pConverter);
    }

    if (SUCCEEDED(hr)) {
        hr = pConverter->Initialize(
            pFrame,
            GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone,
            NULL,
            0.f,
            WICBitmapPaletteTypeMedianCut
        );
    }

    if (SUCCEEDED(hr)) {
        hr = g_pD2DContext->CreateBitmapFromWicBitmap(pConverter, NULL, ppBitmap);
    }

    if (SUCCEEDED(hr)) {
        Log("LoadImageFromFileToD2DBitmap: Successfully loaded and created bitmap for " + WStringToString(filePath));
    } else {
        Log("LoadImageFromFileToD2DBitmap: Failed somewhere in the loading process for " + WStringToString(filePath));
    }

    SAFE_RELEASE(pDecoder);
    SAFE_RELEASE(pFrame);
    SAFE_RELEASE(pConverter);

    return SUCCEEDED(hr);
}