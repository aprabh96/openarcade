#include <string>
#include <vector>
#include <cstdint>
#ifndef OVERLAY_H
#define OVERLAY_H

// <ai_context>
// Overlay-related declarations
// </ai_context>

#include "globals.h"

// Structure for passing game data to main thread
struct GameDataResult {
    std::string appId; // AppID of the game this data is for
    std::string description;
    std::string headerUrl;
    std::vector<uint8_t> imageBytes;
    bool success;
};

// Add these declarations for use in other files
void UpdateCurrentCategoryGameList_VR();

bool InitializeOpenVRForOverlay();
void PrepareOverlay();
void ShowOverlayOriginalMethod();
void ShowOverlayContinuous();
void HideOverlayContinuous();
void StartContinuousOverlayTest();
void StopContinuousOverlayTest();
void LaunchSteamVR();
bool DrawOverlayContentD2D();
bool UpdateOverlayTexture(vr::VROverlayHandle_t overlayHandle);
bool RefreshOverlayTexture(vr::VROverlayHandle_t overlayHandle);

// New test function for video debugging
bool TestVideoPlayback(HWND hWnd);

// Add this for overlay mouse input
void HandleOverlayMouseEvent(const vr::VREvent_t& event);

// Add this for polling overlay events
void PollOverlayEvents();

// New function to handle continuous scrolling when a button is held
void UpdateContinuousScroll();

// Function to load an image from a URL using WinHTTP and WIC into a D2D Bitmap
bool LoadImageFromURLToD2DBitmap(const std::string& appid, const std::string& imageUrl, ID2D1Bitmap** ppBitmap);
bool LoadImageFromFileToD2DBitmap(const std::wstring& filePath, ID2D1Bitmap** ppBitmap);

// Add these declarations for use in other files
#define WM_APP_ENSURE_OVERLAY_VISIBLE (WM_APP + 103)
#define WM_APP_HANDLE_RECREATE_TARGET (WM_APP + 104)

#endif