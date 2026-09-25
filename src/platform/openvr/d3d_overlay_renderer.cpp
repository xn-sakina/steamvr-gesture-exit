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
namespace bailout::platform::openvr {
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
  ComPtr<IDWriteTextFormat> title, body, button, dashboard_title, dashboard_tab,
      dashboard_button, label, help_text, step_number, small_text_format;
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
    HRESULT hr = create(adapter.Get(),
                        adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE);
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
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f);
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
        D2D1::ColorF(0.96f, 0.98f, 1, 1), D2D1::ColorF(0.66f, 0.72f, 0.81f, 1),
        D2D1::ColorF(0.38f, 0.44f, 0.52f, 1), D2D1::ColorF(0.10f, 0.58f, 0.82f, 1),
        D2D1::ColorF(0.20f, 0.74f, 0.94f, 1), D2D1::ColorF(0.075f, 0.12f, 0.18f, 1),
        D2D1::ColorF(0.055f, 0.085f, 0.13f, 0.80f),
        D2D1::ColorF(0.025f, 0.04f, 0.065f, 0.97f),
        D2D1::ColorF(0.10f, 0.24f, 0.34f, 1), D2D1::ColorF(0.35f, 0.86f, 0.62f, 1),
        D2D1::ColorF(0.98f, 0.55f, 0.38f, 1), D2D1::ColorF(0.10f, 0.58f, 0.82f, 0.10f),
        D2D1::ColorF(0.20f, 0.74f, 0.94f, 0.42f),
        D2D1::ColorF(0.10f, 0.58f, 0.82f, 0.18f)};
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
    const HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(IDR_BAILOUT_PNG), RT_RCDATA);
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
  impl_->write->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                 DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 27, L"",
                                 &impl_->body);
  impl_->write->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                 DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 30, L"",
                                 &impl_->button);
  impl_->write->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                 DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 30, L"",
                                 &impl_->dashboard_title);
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
  if (!impl_->title || !impl_->body || !impl_->button || !impl_->dashboard_title ||
      !impl_->dashboard_tab || !impl_->dashboard_button || !impl_->label ||
      !impl_->help_text || !impl_->step_number || !impl_->small_text_format) {
    impl_->error = "CreateTextFormat failed";
    return false;
  }
  impl_->button->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
  impl_->button->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  impl_->dashboard_tab->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
  impl_->dashboard_tab->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  impl_->dashboard_button->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
  impl_->dashboard_button->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
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
  auto white = impl_->brush(t, D2D1::ColorF(0.96f, 0.98f, 1, 1));
  auto muted = impl_->brush(t, D2D1::ColorF(0.69f, 0.74f, 0.82f, 1));
  auto accent = impl_->brush(t, D2D1::ColorF(0.15f, 0.68f, 0.88f, 1));
  auto panel = impl_->brush(t, D2D1::ColorF(0.035f, 0.055f, 0.085f, 0.94f));
  auto hover = impl_->brush(t, D2D1::ColorF(0.12f, 0.34f, 0.45f, 1));
  auto normal = impl_->brush(t, D2D1::ColorF(0.08f, 0.15f, 0.22f, 1));
  if (!white || !muted || !accent || !panel || !hover || !normal) {
    if (recover_target(id, impl_->resource_status) && !retried)
      return render(id, m, true);
    return false;
  }
  t->BeginDraw();
  t->Clear(D2D1::ColorF(0, 0, 0, 0));
  t->FillRoundedRectangle(
      D2D1::RoundedRect(D2D1::RectF(8, 8, float(s.w - 8), float(s.h - 8)), 24, 24), panel.Get());
  t->DrawTextW(m.title.c_str(), static_cast<UINT32>(m.title.size()), impl_->title.Get(),
               D2D1::RectF(42, 30, float(s.w - 42), 100), white.Get());
  t->DrawTextW(m.body.c_str(), static_cast<UINT32>(m.body.size()), impl_->body.Get(),
               D2D1::RectF(42, 105, float(s.w - 42), float(s.h - 105)), muted.Get());
  if (m.progress > 0) {
    float right = 42 + (s.w - 84) * std::clamp(m.progress, 0.0f, 1.0f);
    t->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(42, float(s.h - 38), right, float(s.h - 24)), 7, 7),
        accent.Get());
  }
  if (!m.left_button.empty()) {
    auto r = D2D1::RoundedRect(
        D2D1::RectF(42, float(s.h - 112), float(s.w / 2 - 12), float(s.h - 54)), 12, 12);
    t->FillRoundedRectangle(r, m.left_hover ? hover.Get() : normal.Get());
    t->DrawTextW(m.left_button.c_str(), static_cast<UINT32>(m.left_button.size()),
                 impl_->button.Get(), r.rect, white.Get());
  }
  if (!m.right_button.empty()) {
    auto r = D2D1::RoundedRect(
        D2D1::RectF(float(s.w / 2 + 12), float(s.h - 112), float(s.w - 42), float(s.h - 54)), 12,
        12);
    t->FillRoundedRectangle(r, m.right_hover ? hover.Get() : normal.Get());
    t->DrawTextW(m.right_button.c_str(), static_cast<UINT32>(m.right_button.size()),
                 impl_->button.Get(), r.rect, white.Get());
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
  const HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(IDR_BAILOUT_PNG), RT_RCDATA);
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
    base->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(8, 8, float(s.w - 8), float(s.h - 8)), 24, 24),
        base_panel.Get());
    auto* tech_line = background.dashboard_brushes[11].Get();
    auto* tab_border = background.dashboard_brushes[12].Get();
    auto* tech_glow = background.dashboard_brushes[13].Get();
    const auto tech_center = D2D1::Point2F(744.0f, 390.0f);
    for (const float radius : {190.0f, 228.0f, 270.0f})
      base->DrawEllipse(D2D1::Ellipse(tech_center, radius, radius), tech_line, 1.5f);
    base->DrawLine(D2D1::Point2F(474, tech_center.y), D2D1::Point2F(1000, tech_center.y),
                   tech_line, 1.0f);
    base->DrawLine(D2D1::Point2F(tech_center.x, 120), D2D1::Point2F(tech_center.x, 624),
                   tech_line, 1.0f);
    for (int i = 0; i < 7; ++i) {
      const float x = 514.0f + 72.0f * static_cast<float>(i);
      base->DrawLine(D2D1::Point2F(x, 150), D2D1::Point2F(x + 180, 610), tech_line, 1.0f);
    }
    for (const auto point : {D2D1::Point2F(553, 284), D2D1::Point2F(914, 306),
                             D2D1::Point2F(614, 548), D2D1::Point2F(894, 522)})
      base->FillEllipse(D2D1::Ellipse(point, 3.5f, 3.5f), tech_glow);
    if (background.dashboard_logo) {
      // Keep the complete square mark visible. A gentle edge fade integrates it
      // with the surrounding radar/circuit lines without making it disappear.
      const auto logo_size = background.dashboard_logo->GetSize();
      constexpr float logo_left = 520.0f;
      constexpr float logo_top = 166.0f;
      constexpr float logo_size_pixels = 448.0f;
      constexpr int kFadeSlices = 48;
      for (int slice = 0; slice < kFadeSlices; ++slice) {
        const float start = static_cast<float>(slice) / kFadeSlices;
        const float end = static_cast<float>(slice + 1) / kFadeSlices;
        const float left = logo_left + logo_size_pixels * start;
        const float right = logo_left + logo_size_pixels * end;
        const float source_left = logo_size.width * start;
        const float source_right = logo_size.width * end;
        const float position = (start + end) * 0.5f;
        const float edge_fade = 0.52f + 0.48f * std::sin(position * 3.14159265f);
        base->DrawBitmap(
            background.dashboard_logo.Get(),
            D2D1::RectF(left, logo_top, right, logo_top + logo_size_pixels),
            0.26f * edge_fade, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
            D2D1::RectF(source_left, 0, source_right, logo_size.height));
      }
    }
    const auto tab_shell = D2D1::RoundedRect(D2D1::RectF(44, 88, 980, 148), 16, 16);
    base->FillRoundedRectangle(tab_shell, background.dashboard_brushes[5].Get());
    base->DrawRoundedRectangle(tab_shell, tab_border, 1.0f);
    const size_t row_count = m.active_tab == core::DashboardTab::Settings ? m.settings.size()
                             : m.active_tab == core::DashboardTab::Status ? m.statuses.size()
                                                                           : m.help_lines.size();
    if (m.active_tab == core::DashboardTab::Help && row_count > 1) {
      const float first_center = 182.0f + 48.0f;
      const float last_center = first_center + 120.0f * static_cast<float>(row_count - 1);
      base->DrawLine(D2D1::Point2F(88, first_center + 22),
                     D2D1::Point2F(88, last_center - 22),
                     background.dashboard_brushes[8].Get(), 3.0f);
    }
    for (size_t i = 0; i < row_count; ++i) {
      const bool help = m.active_tab == core::DashboardTab::Help;
      const float top = (help ? 182.0f : 174.0f) + (help ? 120.0f : 72.0f) * i;
      base->FillRoundedRectangle(
          D2D1::RoundedRect(D2D1::RectF(44, top, 980, top + (help ? 96.0f : 64.0f)), 14.0f,
                            14.0f),
          base_card.Get());
      base->FillRoundedRectangle(
          D2D1::RoundedRect(D2D1::RectF(44, top + 13, 48, top + (help ? 83.0f : 51.0f)), 2, 2),
          background.dashboard_brushes[3].Get());
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
  t->DrawTextW(m.title.c_str(), static_cast<UINT32>(m.title.size()),
               impl_->dashboard_title.Get(), D2D1::RectF(44, 26, 620, 66), white.Get());
  if (!m.notice.empty())
    t->DrawTextW(m.notice.c_str(), static_cast<UINT32>(m.notice.size()),
                 impl_->small_text_format.Get(), D2D1::RectF(630, 34, 980, 64), bad.Get());
  for (const auto& tab : core::dashboard_tabs()) {
    const std::wstring* label = &m.settings_title;
    if (tab.tab == core::DashboardTab::Status)
      label = &m.diagnostics_title;
    else if (tab.tab == core::DashboardTab::Help)
      label = &m.help_title;
    const auto rect = D2D1::RectF(tab.bounds.left, tab.bounds.top + 4, tab.bounds.right,
                                 tab.bounds.bottom - 4);
    const auto rounded = D2D1::RoundedRect(rect, 12, 12);
    const bool selected = m.active_tab == tab.tab;
    const bool hovered = m.hovered_tab && *m.hovered_tab == tab.tab;
    t->FillRoundedRectangle(rounded, selected ? active_tab.Get() : normal.Get());
    if (selected)
      t->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(rect.left + 24, rect.bottom - 4,
                                                               rect.right - 24, rect.bottom - 1),
                                               1.5f, 1.5f),
                              hover.Get());
    if (hovered)
      t->DrawRoundedRectangle(rounded, hover.Get(), 1.5f);
    else if (selected)
      t->DrawRoundedRectangle(rounded, accent.Get(), 1.0f);
    t->DrawTextW(label->c_str(), static_cast<UINT32>(label->size()),
                 impl_->dashboard_tab.Get(), rect, selected ? white.Get() : muted.Get());
  }
  if (m.active_tab == core::DashboardTab::Settings) {
    for (size_t i = 0; i < m.settings.size(); ++i) {
      const float top = 174.0f + 72.0f * static_cast<float>(i);
      const auto& row = m.settings[i];
      t->DrawTextW(row.label.c_str(), static_cast<UINT32>(row.label.size()), impl_->label.Get(),
                   D2D1::RectF(64, top + 17, 372, top + 49), white.Get());
      if (!row.value.empty()) {
        const auto value = core::dashboard_timeout_value_rect();
        const auto rect =
            D2D1::RectF(value.left, value.top + 6, value.right, value.bottom - 6);
        t->FillRoundedRectangle(D2D1::RoundedRect(rect, 9, 9), normal.Get());
        t->DrawTextW(row.value.c_str(), static_cast<UINT32>(row.value.size()),
                     impl_->dashboard_button.Get(), rect, white.Get());
      }
    }
    for (const auto& button : m.buttons) {
      const auto rect = D2D1::RectF(button.bounds.left, button.bounds.top + 6,
                                    button.bounds.right, button.bounds.bottom - 6);
      const auto rounded = D2D1::RoundedRect(rect, 11, 11);
      const bool hovered = m.hovered_action && *m.hovered_action == button.action;
      t->FillRoundedRectangle(rounded, button.selected ? accent.Get() : normal.Get());
      if (hovered)
        t->DrawRoundedRectangle(rounded, hover.Get(), 3.0f);
      else if (button.selected)
        t->DrawRoundedRectangle(rounded, white.Get(), 1.5f);
      t->DrawTextW(button.label.c_str(), static_cast<UINT32>(button.label.size()),
                   impl_->dashboard_button.Get(), rect,
                   button.enabled ? white.Get() : faint.Get());
    }
  } else if (m.active_tab == core::DashboardTab::Status) {
    for (size_t i = 0; i < m.statuses.size(); ++i) {
      const float top = 174.0f + 72.0f * static_cast<float>(i);
      const auto& status = m.statuses[i];
      t->DrawTextW(status.label.c_str(), static_cast<UINT32>(status.label.size()),
                   impl_->label.Get(), D2D1::RectF(64, top + 16, 332, top + 49), white.Get());
      auto* tone = status.tone == DashboardStatusTone::Available     ? good.Get()
                   : status.tone == DashboardStatusTone::Unavailable ? bad.Get()
                                                                     : white.Get();
      t->FillEllipse(D2D1::Ellipse(D2D1::Point2F(356, top + 25), 4.5f, 4.5f), tone);
      t->DrawTextW(status.value.c_str(), static_cast<UINT32>(status.value.size()),
                   impl_->label.Get(), D2D1::RectF(374, top + 8, 956, top + 36), tone);
      t->DrawTextW(status.detail.c_str(), static_cast<UINT32>(status.detail.size()),
                   impl_->small_text_format.Get(), D2D1::RectF(374, top + 36, 956, top + 59),
                   muted.Get());
    }
  } else {
    for (size_t i = 0; i < m.help_lines.size(); ++i) {
      const float top = 182.0f + 120.0f * static_cast<float>(i);
      const auto number = std::to_wstring(i + 1);
      const auto center = D2D1::Point2F(88, top + 48);
      t->FillEllipse(D2D1::Ellipse(center, 25, 25), active_tab.Get());
      t->FillEllipse(D2D1::Ellipse(center, 20, 20), accent.Get());
      t->DrawTextW(number.c_str(), static_cast<UINT32>(number.size()), impl_->step_number.Get(),
                   D2D1::RectF(68, top + 28, 108, top + 68), white.Get());
      t->DrawTextW(m.help_lines[i].c_str(), static_cast<UINT32>(m.help_lines[i].size()),
                   impl_->help_text.Get(), D2D1::RectF(132, top + 10, 948, top + 86), white.Get());
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
}  // namespace bailout::platform::openvr
