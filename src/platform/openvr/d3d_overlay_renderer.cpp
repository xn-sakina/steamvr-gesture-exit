#include "platform/openvr/d3d_overlay_renderer.hpp"
#include "resource_ids.h"
#include "openvr.h"
#include <windows.h>
#include <d3d11.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dxgi.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
namespace steamvr_gesture_exit::platform::openvr {
using Microsoft::WRL::ComPtr;
struct D3dOverlayRenderer::Impl {
  struct Surface {
    uint32_t w = 0, h = 0;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID2D1RenderTarget> target;
    ComPtr<ID2D1Bitmap> dashboard_logo;
    bool dashboard_logo_attempted = false;
    std::array<ComPtr<ID2D1SolidColorBrush>, 14> dashboard_brushes;
    std::unique_ptr<Surface> dashboard_background;
    core::DashboardTab dashboard_background_tab = core::DashboardTab::Settings;
    bool dashboard_background_ready = false;
  };
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  ComPtr<ID2D1Factory> d2d;
  ComPtr<IDWriteFactory> write;
  ComPtr<IDWriteTextFormat> title, prompt_title, body, button, brand, dashboard_tab,
      dashboard_button, label, help_text, step_number, small_text_format, countdown_number;
  ComPtr<ID2D1StrokeStyle> round_stroke;
  std::map<std::string, Surface> surfaces;
  std::string error;
  HRESULT resource_status = S_OK;
  std::string pending_recovery_id;
  HRESULT pending_recovery_status = S_OK;
  uint64_t generation = 0;
  bool create_device(ComPtr<ID3D11Device>& next_device, ComPtr<ID3D11DeviceContext>& next_context) {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL got{};
    constexpr D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                                            D3D_FEATURE_LEVEL_10_1};
    ComPtr<IDXGIAdapter> adapter;
    int32_t adapter_index = -1;
    // The default Windows adapter can differ from the one driving the headset.
    if (auto* system = vr::VRSystem())
      system->GetDXGIOutputInfo(&adapter_index);
    if (adapter_index >= 0) {
      ComPtr<IDXGIFactory> factory;
      if (FAILED(CreateDXGIFactory(IID_PPV_ARGS(&factory))) ||
          FAILED(factory->EnumAdapters(static_cast<UINT>(adapter_index), &adapter))) {
        error = "SteamVR DXGI adapter unavailable";
        return false;
      }
    }
    auto create = [&](IDXGIAdapter* selected_adapter, D3D_DRIVER_TYPE driver) {
      HRESULT hr = D3D11CreateDevice(selected_adapter, driver, nullptr, flags, levels, 3,
                                     D3D11_SDK_VERSION, &next_device, &got, &next_context);
      if (hr == E_INVALIDARG) {
        next_device.Reset();
        next_context.Reset();
        hr = D3D11CreateDevice(selected_adapter, driver, nullptr, flags, levels + 1, 2,
                               D3D11_SDK_VERSION, &next_device, &got, &next_context);
      }
      return hr;
    };
    HRESULT hr =
        create(adapter.Get(), adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE);
    if (FAILED(hr) && !adapter) {
      next_device.Reset();
      next_context.Reset();
      hr = create(nullptr, D3D_DRIVER_TYPE_WARP);
    }
    if (FAILED(hr))
      error = "D3D11CreateDevice failed";
    return SUCCEEDED(hr);
  }
  bool make_surface(ID3D11Device* source, uint32_t w, uint32_t h, Surface& s) {
    D3D11_TEXTURE2D_DESC d{};
    d.Width = w;
    d.Height = h;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    s.w = w;
    s.h = h;
    const auto texture_result = source->CreateTexture2D(&d, nullptr, &s.texture);
    if (FAILED(texture_result)) {
      resource_status = texture_result;
      error = "CreateTexture2D failed";
      return false;
    }
    ComPtr<IDXGISurface> dxgi;
    const auto surface_result = s.texture.As(&dxgi);
    if (FAILED(surface_result)) {
      resource_status = surface_result;
      error = "IDXGISurface query failed";
      return false;
    }
    auto props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
    const auto target_result = d2d->CreateDxgiSurfaceRenderTarget(dxgi.Get(), &props, &s.target);
    if (FAILED(target_result)) {
      resource_status = target_result;
      error = "CreateDxgiSurfaceRenderTarget failed";
      return false;
    }
    return true;
  }
  ComPtr<ID2D1SolidColorBrush> brush(ID2D1RenderTarget* t, D2D1_COLOR_F c) {
    ComPtr<ID2D1SolidColorBrush> b;
    const auto result = t->CreateSolidColorBrush(c, &b);
    if (FAILED(result)) {
      resource_status = result;
      error = "CreateSolidColorBrush failed";
    }
    return b;
  }
  bool ensure_dashboard_brushes(Surface& s) {
    const std::array<D2D1_COLOR_F, 14> colors = {
        D2D1::ColorF(0.96f, 0.98f, 1, 1),           D2D1::ColorF(0.66f, 0.72f, 0.81f, 1),
        D2D1::ColorF(0.38f, 0.44f, 0.52f, 1),       D2D1::ColorF(0.10f, 0.58f, 0.82f, 1),
        D2D1::ColorF(0.20f, 0.74f, 0.94f, 1),       D2D1::ColorF(0.075f, 0.12f, 0.18f, 1),
        D2D1::ColorF(0.055f, 0.085f, 0.13f, 0.72f), D2D1::ColorF(0.025f, 0.04f, 0.065f, 0.97f),
        D2D1::ColorF(0.10f, 0.24f, 0.34f, 1),       D2D1::ColorF(0.35f, 0.86f, 0.62f, 1),
        D2D1::ColorF(0.98f, 0.55f, 0.38f, 1),       D2D1::ColorF(0.10f, 0.58f, 0.82f, 0.10f),
        D2D1::ColorF(0.20f, 0.74f, 0.94f, 0.42f),   D2D1::ColorF(0.10f, 0.58f, 0.82f, 0.18f)};
    for (size_t i = 0; i < colors.size(); ++i) {
      if (!s.dashboard_brushes[i])
        s.dashboard_brushes[i] = brush(s.target.Get(), colors[i]);
      if (!s.dashboard_brushes[i])
        return false;
    }
    return true;
  }
  void load_dashboard_logo(Surface& surface) {
    if (surface.dashboard_logo_attempted)
      return;
    surface.dashboard_logo_attempted = true;
    const HMODULE module = GetModuleHandleW(nullptr);
    const HRSRC resource =
        FindResourceW(module, MAKEINTRESOURCEW(IDR_STEAMVR_GESTURE_EXIT_PNG), RT_RCDATA);
    if (!resource)
      return;
    const HGLOBAL loaded = LoadResource(module, resource);
    if (!loaded)
      return;
    const DWORD size = SizeofResource(module, resource);
    BYTE* bytes = static_cast<BYTE*>(LockResource(loaded));
    if (!size || !bytes)
      return;
    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com_result) && com_result != RPC_E_CHANGED_MODE)
      return;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->CreateStream(&stream)) &&
        SUCCEEDED(stream->InitializeFromMemory(bytes, size)) &&
        SUCCEEDED(factory->CreateDecoderFromStream(stream.Get(), nullptr,
                                                   WICDecodeMetadataCacheOnLoad, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) &&
        SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                                        WICBitmapDitherTypeNone, nullptr, 0,
                                        WICBitmapPaletteTypeCustom))) {
      (void)surface.target->CreateBitmapFromWicBitmap(converter.Get(), nullptr,
                                                      &surface.dashboard_logo);
    }
    converter.Reset();
    frame.Reset();
    decoder.Reset();
    stream.Reset();
    factory.Reset();
    if (SUCCEEDED(com_result))
      CoUninitialize();
  }
};
D3dOverlayRenderer::D3dOverlayRenderer() : impl_(std::make_unique<Impl>()) {}
D3dOverlayRenderer::~D3dOverlayRenderer() = default;
bool D3dOverlayRenderer::initialize() {
  if (!impl_->create_device(impl_->device, impl_->context))
    return false;
  if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, impl_->d2d.GetAddressOf())) ||
      FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                 reinterpret_cast<IUnknown**>(impl_->write.GetAddressOf())))) {
    impl_->error = "Direct2D/DirectWrite initialization failed";
    return false;
  }
  impl_->write->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                 DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 46, L"",
                                 &impl_->title);
  impl_->write->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                 DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 38, L"",
                                 &impl_->prompt_title);
  impl_->write->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                 DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 27, L"",
                                 &impl_->body);
  impl_->write->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                 DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 30, L"",
                                 &impl_->button);
  impl_->write->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                 DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 21, L"",
                                 &impl_->brand);
  impl_->write->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                 DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 19, L"",
                                 &impl_->dashboard_tab);
  impl_->write->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                 DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 20, L"",
                                 &impl_->dashboard_button);
  impl_->write->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                 DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 18, L"",
                                 &impl_->label);
  impl_->write->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                 DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 18, L"",
                                 &impl_->help_text);
  impl_->write->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                 DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 18, L"",
                                 &impl_->step_number);
  impl_->write->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                 DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14, L"",
                                 &impl_->small_text_format);
  impl_->write->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                 DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 76, L"",
                                 &impl_->countdown_number);
  D2D1_STROKE_STYLE_PROPERTIES stroke_properties = D2D1::StrokeStyleProperties();
  stroke_properties.startCap = D2D1_CAP_STYLE_ROUND;
  stroke_properties.endCap = D2D1_CAP_STYLE_ROUND;
  (void)impl_->d2d->CreateStrokeStyle(stroke_properties, nullptr, 0, &impl_->round_stroke);
  if (!impl_->title || !impl_->prompt_title || !impl_->body || !impl_->button || !impl_->brand ||
      !impl_->dashboard_tab || !impl_->dashboard_button || !impl_->label || !impl_->help_text ||
      !impl_->step_number || !impl_->small_text_format || !impl_->countdown_number ||
      !impl_->round_stroke) {
    impl_->error = "CreateTextFormat failed";
    return false;
  }
  impl_->button->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
  impl_->button->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  impl_->title->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
  impl_->title->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  impl_->prompt_title->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
  impl_->prompt_title->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  impl_->brand->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
  impl_->brand->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  impl_->countdown_number->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
  impl_->countdown_number->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  impl_->dashboard_tab->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
  impl_->dashboard_tab->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  impl_->dashboard_button->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
  impl_->dashboard_button->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  impl_->label->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  impl_->help_text->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  impl_->step_number->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
  impl_->step_number->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  return true;
}
bool D3dOverlayRenderer::create_surface(const std::string& id, uint32_t w, uint32_t h) {
  impl_->error.clear();
  Impl::Surface s{};
  if (!impl_->make_surface(impl_->device.Get(), w, h, s))
    return false;
  impl_->surfaces[id] = std::move(s);
  return true;
}
bool D3dOverlayRenderer::recover_target(const std::string& id, long result) {
  const auto status = static_cast<HRESULT>(result);
  const bool device_lost = status == DXGI_ERROR_DEVICE_REMOVED ||
                           status == DXGI_ERROR_DEVICE_RESET || status == DXGI_ERROR_DEVICE_HUNG ||
                           status == DXGI_ERROR_DRIVER_INTERNAL_ERROR ||
                           FAILED(impl_->device->GetDeviceRemovedReason());
  if (status != static_cast<HRESULT>(D2DERR_RECREATE_TARGET) && !device_lost) {
    if (impl_->error.empty())
      impl_->error = "render target failed";
    return false;
  }
  impl_->pending_recovery_id = id;
  impl_->pending_recovery_status = status;
  if (!device_lost) {
    const auto it = impl_->surfaces.find(id);
    Impl::Surface next{};
    if (it != impl_->surfaces.end() &&
        impl_->make_surface(impl_->device.Get(), it->second.w, it->second.h, next)) {
      it->second = std::move(next);
      ++impl_->generation;
      impl_->error.clear();
      impl_->pending_recovery_id.clear();
      return true;
    }
    if (SUCCEEDED(impl_->device->GetDeviceRemovedReason()))
      return false;
  }
  ComPtr<ID3D11Device> next_device;
  ComPtr<ID3D11DeviceContext> next_context;
  if (!impl_->create_device(next_device, next_context))
    return false;
  std::map<std::string, Impl::Surface> next_surfaces;
  for (const auto& [key, surface] : impl_->surfaces) {
    Impl::Surface next{};
    if (!impl_->make_surface(next_device.Get(), surface.w, surface.h, next))
      return false;
    next_surfaces.emplace(key, std::move(next));
  }
  impl_->surfaces = std::move(next_surfaces);
  impl_->context = std::move(next_context);
  impl_->device = std::move(next_device);
  ++impl_->generation;
  impl_->error.clear();
  impl_->pending_recovery_id.clear();
  return true;
}
bool D3dOverlayRenderer::retry_pending_recovery() {
  if (impl_->pending_recovery_id.empty())
    return true;
  return recover_target(impl_->pending_recovery_id, impl_->pending_recovery_status);
}
bool D3dOverlayRenderer::render(const std::string& id, const SurfaceModel& m) {
  return render(id, m, false);
}
bool D3dOverlayRenderer::render(const std::string& id, const SurfaceModel& m, bool retried) {
  impl_->error.clear();
  impl_->resource_status = S_OK;
  auto it = impl_->surfaces.find(id);
  if (it == impl_->surfaces.end()) {
    impl_->error = "surface unavailable";
    return false;
  }
  auto& s = it->second;
  auto* t = s.target.Get();
  auto white = impl_->brush(t, D2D1::ColorF(0.96f, 0.98f, 1.0f, 1.0f));
  auto muted = impl_->brush(t, D2D1::ColorF(0.64f, 0.70f, 0.79f, 1.0f));
  auto accent = impl_->brush(t, D2D1::ColorF(0.10f, 0.58f, 0.82f, 1.0f));
  auto accent_hover = impl_->brush(t, D2D1::ColorF(0.20f, 0.74f, 0.94f, 1.0f));
  auto danger = impl_->brush(t, D2D1::ColorF(0.86f, 0.27f, 0.24f, 1.0f));
  auto danger_hover = impl_->brush(t, D2D1::ColorF(0.98f, 0.38f, 0.32f, 1.0f));
  auto panel = impl_->brush(t, D2D1::ColorF(0.025f, 0.04f, 0.065f, 0.97f));
  auto panel_border = impl_->brush(t, D2D1::ColorF(0.20f, 0.74f, 0.94f, 0.72f));
  auto panel_outer_glow = impl_->brush(t, D2D1::ColorF(0.10f, 0.58f, 0.82f, 0.08f));
  auto panel_soft_glow = impl_->brush(t, D2D1::ColorF(0.20f, 0.74f, 0.94f, 0.18f));
  auto panel_inner_highlight = impl_->brush(t, D2D1::ColorF(0.55f, 0.78f, 1.0f, 0.15f));
  auto neutral = impl_->brush(t, D2D1::ColorF(0.075f, 0.12f, 0.18f, 1.0f));
  auto neutral_hover = impl_->brush(t, D2D1::ColorF(0.12f, 0.22f, 0.31f, 1.0f));
  auto cancel = impl_->brush(t, D2D1::ColorF(0.86f, 0.90f, 0.96f, 1.0f));
  auto cancel_hover = impl_->brush(t, D2D1::ColorF(0.97f, 0.99f, 1.0f, 1.0f));
  auto dark_text = impl_->brush(t, D2D1::ColorF(0.025f, 0.04f, 0.065f, 1.0f));
  auto track = impl_->brush(t, D2D1::ColorF(0.33f, 0.40f, 0.49f, 0.28f));
  if (!white || !muted || !accent || !accent_hover || !danger || !danger_hover || !panel ||
      !panel_border || !panel_outer_glow || !panel_soft_glow || !panel_inner_highlight ||
      !neutral || !neutral_hover || !cancel || !cancel_hover || !dark_text || !track) {
    if (recover_target(id, impl_->resource_status) && !retried)
      return render(id, m, true);
    return false;
  }
  t->BeginDraw();
  t->Clear(D2D1::ColorF(0, 0, 0, 0));
  const auto panel_bounds =
      D2D1::RoundedRect(D2D1::RectF(10, 10, float(s.w - 10), float(s.h - 10)), 32, 32);
  // Separate the control from bright or busy game scenes with a restrained,
  // static bloom. The wide strokes are drawn first so the opaque panel hides
  // their inner halves and leaves only a soft exterior halo.
  t->DrawRoundedRectangle(panel_bounds, panel_outer_glow.Get(), 12.0f);
  t->DrawRoundedRectangle(panel_bounds, panel_soft_glow.Get(), 5.0f);
  t->FillRoundedRectangle(panel_bounds, panel.Get());

  ComPtr<ID2D1GradientStopCollection> border_stops;
  ComPtr<ID2D1LinearGradientBrush> border_gradient;
  const D2D1_GRADIENT_STOP border_colors[] = {
      {0.0f, D2D1::ColorF(0.34f, 0.88f, 1.0f, 0.95f)},
      {0.48f, D2D1::ColorF(0.10f, 0.58f, 0.94f, 0.80f)},
      {1.0f, D2D1::ColorF(0.54f, 0.36f, 0.96f, 0.64f)}};
  if (SUCCEEDED(t->CreateGradientStopCollection(border_colors, 3, D2D1_GAMMA_2_2,
                                                 D2D1_EXTEND_MODE_CLAMP, &border_stops))) {
    (void)t->CreateLinearGradientBrush(
        D2D1::LinearGradientBrushProperties(D2D1::Point2F(70.0f, 18.0f),
                                            D2D1::Point2F(470.0f, 500.0f)),
        border_stops.Get(), &border_gradient);
  }
  t->DrawRoundedRectangle(panel_bounds,
                          border_gradient ? static_cast<ID2D1Brush*>(border_gradient.Get())
                                          : static_cast<ID2D1Brush*>(panel_border.Get()),
                          1.75f);
  const auto inner_bounds =
      D2D1::RoundedRect(D2D1::RectF(13, 13, float(s.w - 13), float(s.h - 13)), 29, 29);
  t->DrawRoundedRectangle(inner_bounds, panel_inner_highlight.Get(), 1.0f);
  if (m.mode == SurfaceMode::Message) {
    t->DrawTextW(m.title.c_str(), static_cast<UINT32>(m.title.size()), impl_->title.Get(),
                 D2D1::RectF(42, 30, float(s.w - 42), 100), white.Get());
    t->DrawTextW(m.body.c_str(), static_cast<UINT32>(m.body.size()), impl_->body.Get(),
                 D2D1::RectF(42, 105, float(s.w - 42), float(s.h - 32)), muted.Get());
  } else {
    ComPtr<ID2D1GradientStopCollection> stops;
    ComPtr<ID2D1LinearGradientBrush> wash;
    const D2D1_GRADIENT_STOP wash_stops[] = {{0.0f, D2D1::ColorF(0.025f, 0.04f, 0.065f, 0.10f)},
                                             {1.0f, D2D1::ColorF(0.025f, 0.04f, 0.065f, 0.82f)}};
    if (SUCCEEDED(t->CreateGradientStopCollection(wash_stops, 2, D2D1_GAMMA_2_2,
                                                  D2D1_EXTEND_MODE_CLAMP, &stops)) &&
        SUCCEEDED(t->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(D2D1::Point2F(256, 120), D2D1::Point2F(256, 520)),
            stops.Get(), &wash)))
      t->FillRoundedRectangle(panel_bounds, wash.Get());

    impl_->load_dashboard_logo(s);
    if (m.mode != SurfaceMode::Countdown) {
      if (s.dashboard_logo) {
        const auto logo_size = s.dashboard_logo->GetSize();
        t->DrawBitmap(s.dashboard_logo.Get(), D2D1::RectF(44, 58, 88, 102), 1.0f,
                      D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                      D2D1::RectF(0, 0, logo_size.width, logo_size.height));
      }
      const float timeout = std::clamp(m.timeout_progress, 0.0f, 1.0f);
      const auto timeout_track = D2D1::RoundedRect(D2D1::RectF(40, 30, 472, 38), 4, 4);
      t->FillRoundedRectangle(timeout_track, track.Get());
      if (timeout > 0.0f) {
        t->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(40, 30, 40 + 432 * timeout, 38), 4, 4), accent.Get());
      }
      t->DrawTextW(m.body.c_str(), static_cast<UINT32>(m.body.size()), impl_->brand.Get(),
                   D2D1::RectF(100, 52, 460, 110), muted.Get());

      // A compact pictogram gives the prompt a visual centre without adding
      // explanatory copy. Keep it deliberately light so the actions remain
      // the strongest elements on the panel.
      const auto cue_centre = D2D1::Point2F(256.0f, 184.0f);
      auto cue_fill = impl_->brush(t, D2D1::ColorF(0.10f, 0.58f, 0.82f, 0.12f));
      auto cue_stroke = impl_->brush(t, D2D1::ColorF(0.20f, 0.74f, 0.94f, 0.72f));
      if (!cue_fill || !cue_stroke) {
        (void)t->EndDraw();
        if (recover_target(id, impl_->resource_status) && !retried)
          return render(id, m, true);
        return false;
      }
      t->FillEllipse(D2D1::Ellipse(cue_centre, 30.0f, 30.0f), cue_fill.Get());
      t->DrawEllipse(D2D1::Ellipse(cue_centre, 30.0f, 30.0f), cue_stroke.Get(), 2.0f);
      t->DrawLine(D2D1::Point2F(244.0f, 171.0f), D2D1::Point2F(244.0f, 197.0f),
                  cue_stroke.Get(), 3.0f, impl_->round_stroke.Get());
      t->DrawLine(D2D1::Point2F(244.0f, 171.0f), D2D1::Point2F(254.0f, 171.0f),
                  cue_stroke.Get(), 3.0f, impl_->round_stroke.Get());
      t->DrawLine(D2D1::Point2F(244.0f, 197.0f), D2D1::Point2F(254.0f, 197.0f),
                  cue_stroke.Get(), 3.0f, impl_->round_stroke.Get());
      t->DrawLine(D2D1::Point2F(251.0f, 184.0f), D2D1::Point2F(268.0f, 184.0f),
                  cue_stroke.Get(), 3.0f, impl_->round_stroke.Get());
      t->DrawLine(D2D1::Point2F(262.0f, 178.0f), D2D1::Point2F(268.0f, 184.0f),
                  cue_stroke.Get(), 3.0f, impl_->round_stroke.Get());
      t->DrawLine(D2D1::Point2F(262.0f, 190.0f), D2D1::Point2F(268.0f, 184.0f),
                  cue_stroke.Get(), 3.0f, impl_->round_stroke.Get());
      t->DrawTextW(m.title.c_str(), static_cast<UINT32>(m.title.size()), impl_->prompt_title.Get(),
                   D2D1::RectF(40, 226, 472, 310), white.Get());
    }

    // The countdown belongs to the content area above the fixed button group,
    // rather than to the full surface including the button.
    constexpr float content_top = 10.0f;
    constexpr float button_top = 390.0f;
    const D2D1_POINT_2F ring_centre =
        D2D1::Point2F(256.0f, content_top + (button_top - content_top) * 0.5f);
    constexpr float ring_radius = 112.0f;
    if (m.mode == SurfaceMode::Countdown) {
      constexpr float watermark_radius = 102.0f;
      const auto watermark_bounds =
          D2D1::RectF(ring_centre.x - watermark_radius, ring_centre.y - watermark_radius,
                      ring_centre.x + watermark_radius, ring_centre.y + watermark_radius);
      ComPtr<ID2D1EllipseGeometry> watermark_clip;
      ComPtr<ID2D1Layer> watermark_layer;
      if (s.dashboard_logo &&
          SUCCEEDED(impl_->d2d->CreateEllipseGeometry(
              D2D1::Ellipse(ring_centre, watermark_radius, watermark_radius), &watermark_clip)) &&
          SUCCEEDED(t->CreateLayer(nullptr, &watermark_layer))) {
        const auto logo_size = s.dashboard_logo->GetSize();
        t->PushLayer(D2D1::LayerParameters(watermark_bounds, watermark_clip.Get()),
                     watermark_layer.Get());
        t->DrawBitmap(s.dashboard_logo.Get(), watermark_bounds, 0.08f,
                      D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                      D2D1::RectF(0, 0, logo_size.width, logo_size.height));
        t->PopLayer();
      }
      t->DrawEllipse(D2D1::Ellipse(ring_centre, ring_radius, ring_radius), track.Get(), 14.0f,
                     impl_->round_stroke.Get());
      const float progress = std::clamp(m.progress, 0.0f, 1.0f);
      const float elapsed = 1.0f - progress;
      const D2D1_COLOR_F ring_color = D2D1::ColorF(0.10f + 0.88f * elapsed, 0.58f - 0.24f * elapsed,
                                                   0.82f - 0.54f * elapsed, 1.0f);
      auto ring = impl_->brush(t, ring_color);
      if (!ring) {
        (void)t->EndDraw();
        if (recover_target(id, impl_->resource_status) && !retried)
          return render(id, m, true);
        return false;
      }
      if (progress >= 0.999f) {
        t->DrawEllipse(D2D1::Ellipse(ring_centre, ring_radius, ring_radius), ring.Get(), 14.0f,
                       impl_->round_stroke.Get());
      } else if (progress > 0.0f) {
        const float angle = -3.14159265f / 2.0f + 2.0f * 3.14159265f * progress;
        const auto start = D2D1::Point2F(ring_centre.x, ring_centre.y - ring_radius);
        const auto end = D2D1::Point2F(ring_centre.x + std::cos(angle) * ring_radius,
                                       ring_centre.y + std::sin(angle) * ring_radius);
        ComPtr<ID2D1PathGeometry> geometry;
        ComPtr<ID2D1GeometrySink> sink;
        if (SUCCEEDED(impl_->d2d->CreatePathGeometry(&geometry)) &&
            SUCCEEDED(geometry->Open(&sink))) {
          sink->BeginFigure(start, D2D1_FIGURE_BEGIN_HOLLOW);
          sink->AddArc(D2D1::ArcSegment(
              end, D2D1::SizeF(ring_radius, ring_radius), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE,
              progress > 0.5f ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
          sink->EndFigure(D2D1_FIGURE_END_OPEN);
          if (SUCCEEDED(sink->Close()))
            t->DrawGeometry(geometry.Get(), ring.Get(), 14.0f, impl_->round_stroke.Get());
        }
      }
      const std::wstring seconds = std::to_wstring(std::max(1, m.countdown_seconds));
      t->DrawTextW(seconds.c_str(), static_cast<UINT32>(seconds.size()),
                   impl_->countdown_number.Get(), D2D1::RectF(156, 110, 356, 290), white.Get());
    }

    if (!m.left_button.empty()) {
      const auto bounds = D2D1::RoundedRect(D2D1::RectF(40, 390, 244, 462), 14, 14);
      ID2D1Brush* fill =
          m.mode == SurfaceMode::Confirm
              ? static_cast<ID2D1Brush*>(m.left_hover ? danger_hover.Get() : danger.Get())
              : static_cast<ID2D1Brush*>(m.left_hover ? accent_hover.Get() : accent.Get());
      t->FillRoundedRectangle(bounds, fill);
      t->DrawTextW(m.left_button.c_str(), static_cast<UINT32>(m.left_button.size()),
                   impl_->button.Get(), bounds.rect, white.Get());
    }
    if (!m.right_button.empty()) {
      const float left = m.mode == SurfaceMode::Countdown ? 40.0f : 268.0f;
      const auto bounds = D2D1::RoundedRect(D2D1::RectF(left, 390, 472, 462), 14, 14);
      const bool countdown_cancel = m.mode == SurfaceMode::Countdown;
      ID2D1Brush* fill =
          countdown_cancel
              ? static_cast<ID2D1Brush*>(m.right_hover ? cancel_hover.Get() : cancel.Get())
              : static_cast<ID2D1Brush*>(m.right_hover ? neutral_hover.Get() : neutral.Get());
      t->FillRoundedRectangle(bounds, fill);
      if (countdown_cancel && m.right_hover)
        t->DrawRoundedRectangle(bounds, accent_hover.Get(), 2.0f);
      t->DrawTextW(m.right_button.c_str(), static_cast<UINT32>(m.right_button.size()),
                   impl_->button.Get(), bounds.rect,
                   countdown_cancel ? dark_text.Get() : white.Get());
    }
  }
  if (m.pointer_visible) {
    const auto centre = D2D1::Point2F(std::clamp(m.pointer_x, 0.0f, float(s.w)),
                                      std::clamp(m.pointer_y, 0.0f, float(s.h)));
    t->FillEllipse(D2D1::Ellipse(centre, 10.0f, 10.0f), white.Get());
    t->FillEllipse(D2D1::Ellipse(centre, 5.0f, 5.0f), accent.Get());
  }
  auto result = t->EndDraw();
  if (FAILED(result)) {
    if (recover_target(id, result) && !retried)
      return render(id, m, true);
  }
  if (FAILED(result) && impl_->error.empty())
    impl_->error = "EndDraw failed";
  return SUCCEEDED(result);
}
bool D3dOverlayRenderer::render_thumbnail(const std::string& id, const std::wstring& title,
                                          const std::wstring& description) {
  return render_thumbnail(id, title, description, false);
}
bool D3dOverlayRenderer::render_thumbnail(const std::string& id, const std::wstring& title,
                                          const std::wstring& description, bool retried) {
  impl_->error.clear();
  impl_->resource_status = S_OK;
  const auto it = impl_->surfaces.find(id);
  if (it == impl_->surfaces.end() || it->second.w != 256 || it->second.h != 256) {
    impl_->error = "thumbnail surface unavailable";
    return false;
  }
  const HMODULE module = GetModuleHandleW(nullptr);
  const HRSRC resource =
      FindResourceW(module, MAKEINTRESOURCEW(IDR_STEAMVR_GESTURE_EXIT_PNG), RT_RCDATA);
  if (!resource) {
    impl_->error = "thumbnail PNG resource unavailable";
    return false;
  }
  const HGLOBAL loaded = LoadResource(module, resource);
  if (!loaded) {
    impl_->error = "thumbnail PNG resource unreadable";
    return false;
  }
  const DWORD byte_count = SizeofResource(module, resource);
  BYTE* bytes = static_cast<BYTE*>(LockResource(loaded));
  if (!byte_count || !bytes) {
    impl_->error = "thumbnail PNG resource unreadable";
    return false;
  }

  const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(com_result) && com_result != RPC_E_CHANGED_MODE) {
    impl_->error = "COM initialization for thumbnail failed";
    return false;
  }
  struct ComScope {
    bool uninitialize;
    ~ComScope() {
      if (uninitialize)
        CoUninitialize();
    }
  } com_scope{SUCCEEDED(com_result)};
  ComPtr<IWICImagingFactory> factory;
  ComPtr<IWICStream> stream;
  ComPtr<IWICBitmapDecoder> decoder;
  ComPtr<IWICBitmapFrameDecode> frame;
  ComPtr<IWICFormatConverter> converter;
  ComPtr<ID2D1Bitmap> icon;
  auto* target = it->second.target.Get();
  HRESULT bitmap_status = S_OK;
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&factory))) ||
      FAILED(factory->CreateStream(&stream)) ||
      FAILED(stream->InitializeFromMemory(bytes, byte_count)) ||
      FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad,
                                              &decoder)) ||
      FAILED(decoder->GetFrame(0, &frame)) || FAILED(factory->CreateFormatConverter(&converter)) ||
      FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                                   WICBitmapDitherTypeNone, nullptr, 0,
                                   WICBitmapPaletteTypeCustom)) ||
      FAILED(bitmap_status = target->CreateBitmapFromWicBitmap(converter.Get(), nullptr, &icon))) {
    impl_->error = "decode embedded thumbnail PNG failed";
    if (FAILED(bitmap_status) && recover_target(id, bitmap_status) && !retried)
      return render_thumbnail(id, title, description, true);
    return false;
  }
  auto panel = impl_->brush(target, D2D1::ColorF(0x061322, 0.98f));
  auto white = impl_->brush(target, D2D1::ColorF(0xf5fbff, 1.0f));
  auto muted = impl_->brush(target, D2D1::ColorF(0xaabccd, 1.0f));
  ComPtr<IDWriteTextFormat> title_format, description_format;
  if (!panel || !white || !muted ||
      FAILED(impl_->write->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                            21, L"", &title_format)) ||
      FAILED(impl_->write->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                            15, L"", &description_format)) ||
      FAILED(title_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER)) ||
      FAILED(title_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) ||
      FAILED(description_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER)) ||
      FAILED(description_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER))) {
    impl_->error = "thumbnail text setup failed";
    if (!panel || !white || !muted) {
      if (recover_target(id, impl_->resource_status) && !retried)
        return render_thumbnail(id, title, description, true);
    }
    return false;
  }
  target->BeginDraw();
  target->Clear(D2D1::ColorF(0, 0, 0, 0));
  target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(8, 8, 248, 248), 24, 24), panel.Get());
  target->DrawBitmap(icon.Get(), D2D1::RectF(40, 8, 216, 184), 1.0f,
                     D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
  target->DrawTextW(title.c_str(), static_cast<UINT32>(title.size()), title_format.Get(),
                    D2D1::RectF(12, 183, 244, 216), white.Get());
  target->DrawTextW(description.c_str(), static_cast<UINT32>(description.size()),
                    description_format.Get(), D2D1::RectF(12, 217, 244, 245), muted.Get());
  const auto result = target->EndDraw();
  if (FAILED(result)) {
    if (recover_target(id, result) && !retried)
      return render_thumbnail(id, title, description, true);
  }
  if (FAILED(result) && impl_->error.empty())
    impl_->error = "EndDraw(thumbnail) failed";
  return SUCCEEDED(result);
}
bool D3dOverlayRenderer::render_dashboard(const std::string& id, const DashboardSurfaceModel& m) {
  return render_dashboard(id, m, false);
}
bool D3dOverlayRenderer::render_dashboard(const std::string& id, const DashboardSurfaceModel& m,
                                          bool retried) {
  impl_->error.clear();
  impl_->resource_status = S_OK;
  auto it = impl_->surfaces.find(id);
  if (it == impl_->surfaces.end())
    return false;
  auto& s = it->second;
  auto* t = s.target.Get();
  if (!impl_->ensure_dashboard_brushes(s)) {
    if (recover_target(id, impl_->resource_status) && !retried)
      return render_dashboard(id, m, true);
    return false;
  }
  auto& white = s.dashboard_brushes[0];
  auto& muted = s.dashboard_brushes[1];
  auto& faint = s.dashboard_brushes[2];
  auto& accent = s.dashboard_brushes[3];
  auto& hover = s.dashboard_brushes[4];
  auto& normal = s.dashboard_brushes[5];
  auto& active_tab = s.dashboard_brushes[8];
  auto& good = s.dashboard_brushes[9];
  auto& bad = s.dashboard_brushes[10];
  // The panel, cards and logo change only with the tab. Keep their GPU result
  // and copy it before drawing the current labels and selection state.
  if (!s.dashboard_background) {
    auto background = std::make_unique<Impl::Surface>();
    if (!impl_->make_surface(impl_->device.Get(), s.w, s.h, *background) ||
        !impl_->ensure_dashboard_brushes(*background)) {
      if (recover_target(id, impl_->resource_status) && !retried)
        return render_dashboard(id, m, true);
      return false;
    }
    s.dashboard_background = std::move(background);
  }
  auto& background = *s.dashboard_background;
  if (!s.dashboard_background_ready || s.dashboard_background_tab != m.active_tab) {
    auto* base = background.target.Get();
    auto& base_card = background.dashboard_brushes[6];
    auto& base_panel = background.dashboard_brushes[7];
    impl_->load_dashboard_logo(background);
    base->BeginDraw();
    base->Clear(D2D1::ColorF(0, 0, 0, 0));
    base->FillRectangle(D2D1::RectF(8, 8, float(s.w - 8), float(s.h - 8)), base_panel.Get());
    if (background.dashboard_logo) {
      const auto logo_size = background.dashboard_logo->GetSize();
      constexpr float logo_left = 584.0f;
      constexpr float logo_top = 60.0f;
      constexpr float logo_size_pixels = 380.0f;
      base->DrawBitmap(background.dashboard_logo.Get(),
                       D2D1::RectF(logo_left, logo_top, logo_left + logo_size_pixels,
                                   logo_top + logo_size_pixels),
                       0.38f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                       D2D1::RectF(0, 0, logo_size.width, logo_size.height));
    }
    base->FillRectangle(D2D1::RectF(44, 24, 980, 76), background.dashboard_brushes[5].Get());
    base->FillRectangle(D2D1::RectF(355, 34, 356, 66), background.dashboard_brushes[8].Get());
    base->FillRectangle(D2D1::RectF(667, 34, 668, 66), background.dashboard_brushes[8].Get());
    const size_t row_count = m.active_tab == core::DashboardTab::Settings ? m.settings.size()
                             : m.active_tab == core::DashboardTab::Status ? m.statuses.size()
                                                                          : m.help_lines.size();
    for (size_t i = 0; i < row_count; ++i) {
      const bool help = m.active_tab == core::DashboardTab::Help;
      const float row_height = help ? 116.0f : 56.0f;
      const float top = 96.0f + row_height * static_cast<float>(i);
      base->FillRectangle(D2D1::RectF(44, top + 4, 980, top + (help ? 104.0f : row_height - 4.0f)),
                          base_card.Get());
    }
    const auto background_result = base->EndDraw();
    if (FAILED(background_result)) {
      if (recover_target(id, background_result) && !retried)
        return render_dashboard(id, m, true);
      return false;
    }
    s.dashboard_background_ready = true;
    s.dashboard_background_tab = m.active_tab;
  }
  impl_->context->CopyResource(s.texture.Get(), background.texture.Get());
  t->BeginDraw();
  if (m.active_tab == core::DashboardTab::Settings && !m.notice.empty())
    t->DrawTextW(m.notice.c_str(), static_cast<UINT32>(m.notice.size()),
                 impl_->small_text_format.Get(), D2D1::RectF(620, 78, 980, 96),
                 m.notice_tone == DashboardNoticeTone::Success ? good.Get() : bad.Get());
  for (const auto& tab : core::dashboard_tabs()) {
    const std::wstring* label = &m.settings_title;
    if (tab.tab == core::DashboardTab::Status)
      label = &m.diagnostics_title;
    else if (tab.tab == core::DashboardTab::Help)
      label = &m.help_title;
    const auto rect =
        D2D1::RectF(tab.bounds.left, tab.bounds.top + 4, tab.bounds.right, tab.bounds.bottom - 4);
    const bool selected = m.active_tab == tab.tab;
    const bool hovered = m.hovered_tab && *m.hovered_tab == tab.tab;
    if (selected)
      t->FillRectangle(rect, active_tab.Get());
    if (selected)
      t->FillRectangle(D2D1::RectF(rect.left, rect.bottom - 3, rect.right, rect.bottom),
                       hover.Get());
    if (hovered)
      t->DrawRectangle(rect, hover.Get(), 2.0f);
    t->DrawTextW(label->c_str(), static_cast<UINT32>(label->size()), impl_->dashboard_tab.Get(),
                 rect, selected || hovered ? white.Get() : muted.Get());
  }
  if (m.active_tab == core::DashboardTab::Settings) {
    for (size_t i = 0; i < m.settings.size(); ++i) {
      const float top = 96.0f + 56.0f * static_cast<float>(i);
      const auto& row = m.settings[i];
      t->DrawTextW(row.label.c_str(), static_cast<UINT32>(row.label.size()), impl_->label.Get(),
                   D2D1::RectF(64, top + 2, 372, top + 56), white.Get());
      if (!row.value.empty()) {
        const auto value = core::dashboard_timeout_value_rect();
        const auto rect = D2D1::RectF(value.left, value.top + 4, value.right, value.bottom - 4);
        t->FillRectangle(rect, normal.Get());
        t->DrawTextW(row.value.c_str(), static_cast<UINT32>(row.value.size()),
                     impl_->dashboard_button.Get(), rect, white.Get());
      }
    }
    for (const auto& button : m.buttons) {
      const auto rect = D2D1::RectF(button.bounds.left, button.bounds.top + 4, button.bounds.right,
                                    button.bounds.bottom - 4);
      const bool hovered = m.hovered_action && *m.hovered_action == button.action;
      t->FillRectangle(rect, button.selected ? accent.Get() : normal.Get());
      if (hovered)
        t->DrawRectangle(rect, hover.Get(), 2.0f);
      t->DrawTextW(button.label.c_str(), static_cast<UINT32>(button.label.size()),
                   impl_->dashboard_button.Get(), rect, button.enabled ? white.Get() : faint.Get());
    }
  } else if (m.active_tab == core::DashboardTab::Status) {
    for (size_t i = 0; i < m.statuses.size(); ++i) {
      const float top = 96.0f + 56.0f * static_cast<float>(i);
      const auto& status = m.statuses[i];
      t->DrawTextW(status.label.c_str(), static_cast<UINT32>(status.label.size()),
                   impl_->label.Get(), D2D1::RectF(64, top + 4, 344, top + 52), white.Get());
      auto* tone = status.tone == DashboardStatusTone::Available     ? good.Get()
                   : status.tone == DashboardStatusTone::Unavailable ? bad.Get()
                                                                     : white.Get();
      t->DrawTextW(status.value.c_str(), static_cast<UINT32>(status.value.size()),
                   impl_->label.Get(), D2D1::RectF(356, top + 4, 956, top + 52), tone);
    }
  } else {
    for (size_t i = 0; i < m.help_lines.size(); ++i) {
      const float top = 96.0f + 116.0f * static_cast<float>(i);
      const auto number = std::to_wstring(i + 1);
      const auto badge = D2D1::RectF(64, top + 28, 112, top + 76);
      t->FillEllipse(D2D1::Ellipse(D2D1::Point2F(88, top + 52), 24, 24), accent.Get());
      t->DrawTextW(number.c_str(), static_cast<UINT32>(number.size()), impl_->step_number.Get(),
                   badge, white.Get());
      t->DrawTextW(m.help_lines[i].c_str(), static_cast<UINT32>(m.help_lines[i].size()),
                   impl_->help_text.Get(), D2D1::RectF(132, top + 4, 948, top + 104), white.Get());
    }
  }
  const auto result = t->EndDraw();
  if (FAILED(result)) {
    if (recover_target(id, result) && !retried)
      return render_dashboard(id, m, true);
  }
  if (FAILED(result) && impl_->error.empty())
    impl_->error = "EndDraw(dashboard) failed";
  return SUCCEEDED(result);
}
void D3dOverlayRenderer::flush() {
  if (impl_->context)
    impl_->context->Flush();
}
void* D3dOverlayRenderer::texture(const std::string& id) const {
  auto it = impl_->surfaces.find(id);
  return it == impl_->surfaces.end() ? nullptr : it->second.texture.Get();
}
uint64_t D3dOverlayRenderer::generation() const {
  return impl_->generation;
}
const std::string& D3dOverlayRenderer::error() const {
  return impl_->error;
}
}  // namespace steamvr_gesture_exit::platform::openvr
