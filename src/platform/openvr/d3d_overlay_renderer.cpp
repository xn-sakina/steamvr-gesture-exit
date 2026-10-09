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
namespace {
float clamp01(const float value) {
  return std::clamp(value, 0.0f, 1.0f);
}

float ease_out_cubic(const float value) {
  const float inverse = 1.0f - clamp01(value);
  return 1.0f - inverse * inverse * inverse;
}

void append_arc(std::vector<D2D1_POINT_2F>& points, const float centre_x, const float centre_y,
                const float radius, const float start_angle) {
  constexpr int kCornerSteps = 10;
  constexpr float kHalfPi = 0.5f * 3.14159265358979323846f;
  for (int step = 1; step <= kCornerSteps; ++step) {
    const float angle = start_angle + kHalfPi * step / kCornerSteps;
    points.push_back(
        D2D1::Point2F(centre_x + std::cos(angle) * radius, centre_y + std::sin(angle) * radius));
  }
}

// Keep the fill, glow, full border and timeout border on one shared perimeter.
// A flattened point list is intentional here: Direct2D can anti-alias the
// closed geometry, while the same points also give the animated border an
// unambiguous distance along the irregular outline (including the tail).
std::vector<D2D1_POINT_2F> speech_bubble_outline(const D2D1_RECT_F rect, const float radius,
                                                 const bool tail_on_right, const float tail_tip_x) {
  constexpr float kPi = 3.14159265358979323846f;
  constexpr float kTailTop = 410.0f;
  constexpr float kTailTipY = 430.0f;
  constexpr float kTailBottom = 450.0f;
  std::vector<D2D1_POINT_2F> points;
  points.reserve(52);
  const float centre_x = (rect.left + rect.right) * 0.5f;
  points.push_back(D2D1::Point2F(centre_x, rect.top));
  points.push_back(D2D1::Point2F(rect.right - radius, rect.top));
  append_arc(points, rect.right - radius, rect.top + radius, radius, -0.5f * kPi);
  if (tail_on_right) {
    points.push_back(D2D1::Point2F(rect.right, kTailTop));
    points.push_back(D2D1::Point2F(tail_tip_x, kTailTipY));
    points.push_back(D2D1::Point2F(rect.right, kTailBottom));
  }
  points.push_back(D2D1::Point2F(rect.right, rect.bottom - radius));
  append_arc(points, rect.right - radius, rect.bottom - radius, radius, 0.0f);
  points.push_back(D2D1::Point2F(rect.left + radius, rect.bottom));
  append_arc(points, rect.left + radius, rect.bottom - radius, radius, 0.5f * kPi);
  if (!tail_on_right) {
    points.push_back(D2D1::Point2F(rect.left, kTailBottom));
    points.push_back(D2D1::Point2F(tail_tip_x, kTailTipY));
    points.push_back(D2D1::Point2F(rect.left, kTailTop));
  }
  points.push_back(D2D1::Point2F(rect.left, rect.top + radius));
  append_arc(points, rect.left + radius, rect.top + radius, radius, kPi);
  points.push_back(points.front());
  // Mirror the traversal as well as the shape. The timeout border therefore
  // always leaves the top centre toward the avatar side and reaches the tail
  // at the same visual moment in left- and right-handed layouts.
  if (!tail_on_right)
    std::reverse(points.begin(), points.end());
  return points;
}

ComPtr<ID2D1PathGeometry> closed_geometry(ID2D1Factory* factory,
                                          const std::vector<D2D1_POINT_2F>& points) {
  ComPtr<ID2D1PathGeometry> geometry;
  ComPtr<ID2D1GeometrySink> sink;
  if (points.size() < 3 || FAILED(factory->CreatePathGeometry(&geometry)) ||
      FAILED(geometry->Open(&sink)))
    return {};
  sink->BeginFigure(points.front(), D2D1_FIGURE_BEGIN_FILLED);
  sink->AddLines(points.data() + 1, static_cast<UINT32>(points.size() - 1));
  sink->EndFigure(D2D1_FIGURE_END_CLOSED);
  if (FAILED(sink->Close()))
    return {};
  return geometry;
}

bool draw_partial_outline(ID2D1Factory* factory, ID2D1RenderTarget* target,
                          const std::vector<D2D1_POINT_2F>& points, const float progress,
                          ID2D1Brush* brush, const float width, ID2D1StrokeStyle* stroke) {
  const float visible = clamp01(progress);
  if (visible <= 0.0f)
    return true;

  std::vector<float> cumulative(points.size(), 0.0f);
  for (size_t index = 1; index < points.size(); ++index) {
    const float dx = points[index].x - points[index - 1].x;
    const float dy = points[index].y - points[index - 1].y;
    cumulative[index] = cumulative[index - 1] + std::sqrt(dx * dx + dy * dy);
  }
  const float target_length = cumulative.back() * visible;
  ComPtr<ID2D1PathGeometry> geometry;
  ComPtr<ID2D1GeometrySink> sink;
  if (FAILED(factory->CreatePathGeometry(&geometry)) || FAILED(geometry->Open(&sink)))
    return false;
  sink->BeginFigure(points.front(), D2D1_FIGURE_BEGIN_HOLLOW);
  for (size_t index = 1; index < points.size(); ++index) {
    if (cumulative[index] <= target_length) {
      sink->AddLine(points[index]);
      continue;
    }
    const float segment = cumulative[index] - cumulative[index - 1];
    const float amount = segment > 0.0f ? (target_length - cumulative[index - 1]) / segment : 0.0f;
    if (amount > 0.0f) {
      sink->AddLine(
          D2D1::Point2F(points[index - 1].x + (points[index].x - points[index - 1].x) * amount,
                        points[index - 1].y + (points[index].y - points[index - 1].y) * amount));
    }
    break;
  }
  sink->EndFigure(D2D1_FIGURE_END_OPEN);
  if (FAILED(sink->Close()))
    return false;
  target->DrawGeometry(geometry.Get(), brush, width, stroke);
  return true;
}
}  // namespace

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
                                 DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 34, L"",
                                 &impl_->prompt_title);
  impl_->write->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                 DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 27, L"",
                                 &impl_->body);
  impl_->write->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                 DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 25, L"",
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
  stroke_properties.lineJoin = D2D1_LINE_JOIN_ROUND;
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
  auto neutral = impl_->brush(t, D2D1::ColorF(0.075f, 0.12f, 0.18f, 1.0f));
  auto neutral_hover = impl_->brush(t, D2D1::ColorF(0.12f, 0.22f, 0.31f, 1.0f));
  auto cancel = impl_->brush(t, D2D1::ColorF(0.86f, 0.90f, 0.96f, 1.0f));
  auto cancel_hover = impl_->brush(t, D2D1::ColorF(0.97f, 0.99f, 1.0f, 1.0f));
  auto dark_text = impl_->brush(t, D2D1::ColorF(0.025f, 0.04f, 0.065f, 1.0f));
  auto track = impl_->brush(t, D2D1::ColorF(0.33f, 0.40f, 0.49f, 0.28f));
  if (!white || !muted || !accent || !accent_hover || !danger || !danger_hover || !panel ||
      !panel_border || !panel_outer_glow || !panel_soft_glow || !neutral || !neutral_hover ||
      !cancel || !cancel_hover || !dark_text || !track) {
    if (recover_target(id, impl_->resource_status) && !retried)
      return render(id, m, true);
    return false;
  }
  t->BeginDraw();
  t->Clear(D2D1::ColorF(0, 0, 0, 0));
  if (m.mode == SurfaceMode::Message) {
    const auto panel_bounds =
        D2D1::RoundedRect(D2D1::RectF(10, 10, float(s.w - 10), float(s.h - 10)), 32, 32);
    t->DrawRoundedRectangle(panel_bounds, panel_outer_glow.Get(), 12.0f);
    t->DrawRoundedRectangle(panel_bounds, panel_soft_glow.Get(), 5.0f);
    t->FillRoundedRectangle(panel_bounds, panel.Get());
    t->DrawRoundedRectangle(panel_bounds, panel_border.Get(), 1.75f);
    t->DrawTextW(m.title.c_str(), static_cast<UINT32>(m.title.size()), impl_->title.Get(),
                 D2D1::RectF(42, 30, float(s.w - 42), 100), white.Get());
    t->DrawTextW(m.body.c_str(), static_cast<UINT32>(m.body.size()), impl_->body.Get(),
                 D2D1::RectF(42, 105, float(s.w - 42), float(s.h - 32)), muted.Get());
  } else {
    impl_->load_dashboard_logo(s);
    const float animation = clamp01(m.animation_progress);
    const float panel_left = m.avatar_on_right ? 10.0f : 138.0f;
    const float panel_right = panel_left + 492.0f;
    const float avatar_x = m.avatar_on_right ? 572.0f : 68.0f;
    constexpr float avatar_y = 430.0f;
    constexpr float prompt_panel_top = 222.0f;
    constexpr float expanded_panel_top = 10.0f;
    constexpr float panel_bottom = 502.0f;
    constexpr float button_top = 400.0f;
    constexpr float button_bottom = 468.0f;
    constexpr float panel_border_width = 4.0f;
    constexpr float avatar_border_radius = 42.0f;
    constexpr float avatar_border_gap = 8.0f;
    const float expansion =
        m.mode == SurfaceMode::Countdown ? ease_out_cubic(animation / 0.60f) : 0.0f;
    const float panel_top = prompt_panel_top + (expanded_panel_top - prompt_panel_top) * expansion;
    const auto panel_rect = D2D1::RectF(panel_left, panel_top, panel_right, panel_bottom);
    // Include both half-strokes so avatar_border_gap is the visible gap,
    // rather than merely the distance between the two stroke centre lines.
    const float tail_tip_x =
        avatar_x + (m.avatar_on_right ? -1.0f : 1.0f) *
                       (avatar_border_radius + panel_border_width + avatar_border_gap);
    const auto bubble_points =
        speech_bubble_outline(panel_rect, 30.0f, m.avatar_on_right, tail_tip_x);
    const auto bubble_geometry = closed_geometry(impl_->d2d.Get(), bubble_points);
    if (!bubble_geometry) {
      (void)t->EndDraw();
      impl_->error = "CreatePathGeometry(speech bubble) failed";
      return false;
    }

    ComPtr<ID2D1GradientStopCollection> border_stops;
    const D2D1_GRADIENT_STOP border_colors[] = {{0.0f, D2D1::ColorF(0.34f, 0.88f, 1.0f, 0.98f)},
                                                {0.50f, D2D1::ColorF(0.10f, 0.58f, 0.94f, 0.90f)},
                                                {1.0f, D2D1::ColorF(0.54f, 0.36f, 0.96f, 0.78f)}};
    (void)t->CreateGradientStopCollection(border_colors, 3, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP,
                                          &border_stops);
    auto make_border_gradient = [&](const D2D1_POINT_2F start, const D2D1_POINT_2F end) {
      ComPtr<ID2D1LinearGradientBrush> gradient;
      if (border_stops) {
        (void)t->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(start, end),
                                           border_stops.Get(), &gradient);
      }
      return gradient;
    };
    auto border_gradient = make_border_gradient(D2D1::Point2F(panel_left, panel_top),
                                                D2D1::Point2F(panel_right, panel_bottom));
    auto avatar_border_gradient = make_border_gradient(
        D2D1::Point2F(avatar_x - avatar_border_radius, avatar_y - avatar_border_radius),
        D2D1::Point2F(avatar_x + avatar_border_radius, avatar_y + avatar_border_radius));
    auto* active_border = border_gradient ? static_cast<ID2D1Brush*>(border_gradient.Get())
                                          : static_cast<ID2D1Brush*>(panel_border.Get());
    auto* avatar_border = avatar_border_gradient
                              ? static_cast<ID2D1Brush*>(avatar_border_gradient.Get())
                              : static_cast<ID2D1Brush*>(panel_border.Get());

    float bubble_opacity = 1.0f;
    float bubble_scale = 1.0f;
    float bubble_shift = 0.0f;
    if (m.mode != SurfaceMode::Countdown) {
      const float bubble_phase = ease_out_cubic((animation - 0.42f) / 0.58f);
      bubble_opacity = bubble_phase;
      bubble_scale = 0.96f + 0.04f * bubble_phase;
      bubble_shift = (m.avatar_on_right ? 10.0f : -10.0f) * (1.0f - bubble_phase);
    }
    const float anchor_x = m.avatar_on_right ? panel_right : panel_left;
    const auto bubble_transform =
        D2D1::Matrix3x2F::Scale(bubble_scale, bubble_scale, D2D1::Point2F(anchor_x, 452.0f)) *
        D2D1::Matrix3x2F::Translation(bubble_shift, 7.0f * (1.0f - bubble_opacity));
    t->SetTransform(bubble_transform);
    ComPtr<ID2D1Layer> bubble_layer;
    const bool has_bubble_layer = SUCCEEDED(t->CreateLayer(nullptr, &bubble_layer)) && bubble_layer;
    if (has_bubble_layer) {
      t->PushLayer(
          D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                D2D1::IdentityMatrix(), bubble_opacity),
          bubble_layer.Get());
    }

    t->DrawGeometry(bubble_geometry.Get(), panel_outer_glow.Get(), 12.0f);
    t->DrawGeometry(bubble_geometry.Get(), panel_soft_glow.Get(), 5.0f);
    t->FillGeometry(bubble_geometry.Get(), panel.Get());
    if (m.mode == SurfaceMode::Countdown) {
      t->DrawGeometry(bubble_geometry.Get(), active_border, panel_border_width,
                      impl_->round_stroke.Get());
    } else {
      (void)draw_partial_outline(impl_->d2d.Get(), t, bubble_points, m.timeout_progress,
                                 active_border, panel_border_width, impl_->round_stroke.Get());
      // Centre the title in the optical content band between the border's
      // inner edge and the button row, rather than in the panel's raw bounds.
      const float title_centre = (prompt_panel_top + panel_border_width * 0.5f + button_top) * 0.5f;
      t->DrawTextW(m.title.c_str(), static_cast<UINT32>(m.title.size()), impl_->prompt_title.Get(),
                   D2D1::RectF(panel_left + 34.0f, title_centre - 40.0f, panel_right - 34.0f,
                               title_centre + 40.0f),
                   white.Get());
    }

    // The ring is centred between the expanded panel's inner top edge and
    // the fixed button row. Its outer stroke therefore has equal clear space
    // above and below without moving the buttons.
    const float ring_centre_y =
        (expanded_panel_top + panel_border_width * 0.5f + button_top) * 0.5f;
    const D2D1_POINT_2F ring_centre =
        D2D1::Point2F((panel_left + panel_right) * 0.5f, ring_centre_y);
    constexpr float ring_radius = 108.0f;
    const float ring_scale =
        m.mode == SurfaceMode::Countdown ? ease_out_cubic((animation - 0.60f) / 0.25f) : 0.0f;
    if (ring_scale > 0.001f) {
      t->SetTransform(D2D1::Matrix3x2F::Scale(ring_scale, ring_scale, ring_centre));
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
      // Hold the visual ring at its starting state until the panel and ring
      // have completed their entrance, then ease into the live value without
      // a visible jump. The state machine clock remains intact.
      const float live_progress = clamp01(m.progress);
      const float countdown_phase = ease_out_cubic((animation - 0.85f) / 0.15f);
      const float progress = 1.0f + (live_progress - 1.0f) * countdown_phase;
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
                   impl_->countdown_number.Get(),
                   D2D1::RectF(ring_centre.x - 100.0f, ring_centre.y - 90.0f,
                               ring_centre.x + 100.0f, ring_centre.y + 90.0f),
                   white.Get());
      t->SetTransform(bubble_transform);
    }

    const float left_button_left = panel_left + 30.0f;
    const float left_button_right = panel_left + 234.0f;
    const float right_button_left = panel_left + 258.0f;
    const float right_button_right = panel_left + 462.0f;
    if (!m.left_button.empty()) {
      const auto bounds = D2D1::RoundedRect(
          D2D1::RectF(left_button_left, button_top, left_button_right, button_bottom), 34, 34);
      ID2D1Brush* fill =
          m.mode == SurfaceMode::Confirm
              ? static_cast<ID2D1Brush*>(m.left_hover ? danger_hover.Get() : danger.Get())
              : static_cast<ID2D1Brush*>(m.left_hover ? accent_hover.Get() : accent.Get());
      t->FillRoundedRectangle(bounds, fill);
      t->DrawTextW(m.left_button.c_str(), static_cast<UINT32>(m.left_button.size()),
                   impl_->button.Get(), bounds.rect, white.Get());
    }
    if (!m.right_button.empty()) {
      const float left = m.mode == SurfaceMode::Countdown ? left_button_left : right_button_left;
      const auto bounds = D2D1::RoundedRect(
          D2D1::RectF(left, button_top, right_button_right, button_bottom), 34, 34);
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

    if (has_bubble_layer)
      t->PopLayer();
    t->SetTransform(D2D1::Matrix3x2F::Identity());

    // The avatar enters first, quickly and without overshoot; its compact
    // scale-only motion remains comfortable in a head-tracked view.
    const float avatar_scale =
        m.mode == SurfaceMode::Countdown ? 1.0f : ease_out_cubic(animation / 0.45f);
    if (avatar_scale > 0.001f && s.dashboard_logo) {
      t->SetTransform(
          D2D1::Matrix3x2F::Scale(avatar_scale, avatar_scale, D2D1::Point2F(avatar_x, avatar_y)));
      t->FillEllipse(D2D1::Ellipse(D2D1::Point2F(avatar_x, avatar_y), 48.0f, 48.0f),
                     panel_soft_glow.Get());
      t->FillEllipse(D2D1::Ellipse(D2D1::Point2F(avatar_x, avatar_y), 44.0f, 44.0f), panel.Get());
      const auto logo_size = s.dashboard_logo->GetSize();
      const auto avatar_bounds =
          D2D1::RectF(avatar_x - 41.0f, avatar_y - 41.0f, avatar_x + 41.0f, avatar_y + 41.0f);
      ComPtr<ID2D1EllipseGeometry> avatar_clip;
      ComPtr<ID2D1Layer> avatar_layer;
      if (SUCCEEDED(impl_->d2d->CreateEllipseGeometry(
              D2D1::Ellipse(D2D1::Point2F(avatar_x, avatar_y), 41.0f, 41.0f), &avatar_clip)) &&
          SUCCEEDED(t->CreateLayer(nullptr, &avatar_layer))) {
        t->PushLayer(D2D1::LayerParameters(avatar_bounds, avatar_clip.Get()), avatar_layer.Get());
        t->DrawBitmap(s.dashboard_logo.Get(), avatar_bounds, 1.0f,
                      D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                      D2D1::RectF(0, 0, logo_size.width, logo_size.height));
        t->PopLayer();
      }
      t->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(avatar_x, avatar_y), 42.0f, 42.0f), avatar_border,
                     panel_border_width);
      t->SetTransform(D2D1::Matrix3x2F::Identity());
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
  const auto status_row_rect = [&](size_t index) {
    constexpr float content_top = 100.0f;
    constexpr float row_gap = 8.0f;
    // Match the settings cards: 20 px inside the panel's 8 px outer inset.
    constexpr float bottom_padding = 28.0f;
    const float count = static_cast<float>(m.statuses.size());
    const float height =
        (static_cast<float>(s.h) - bottom_padding - content_top - row_gap * (count - 1)) /
        count;
    const float top = content_top + (height + row_gap) * static_cast<float>(index);
    return D2D1::RectF(44, top, 980, top + height);
  };
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
      if (m.active_tab == core::DashboardTab::Status) {
        base->FillRectangle(status_row_rect(i), base_card.Get());
        continue;
      }
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
      const auto rect = status_row_rect(i);
      const auto& status = m.statuses[i];
      t->DrawTextW(status.label.c_str(), static_cast<UINT32>(status.label.size()),
                   impl_->label.Get(), D2D1::RectF(64, rect.top, 344, rect.bottom), white.Get());
      auto* tone = status.tone == DashboardStatusTone::Available     ? good.Get()
                   : status.tone == DashboardStatusTone::Unavailable ? bad.Get()
                                                                     : white.Get();
      t->DrawTextW(status.value.c_str(), static_cast<UINT32>(status.value.size()),
                   impl_->label.Get(), D2D1::RectF(356, rect.top, 956, rect.bottom), tone);
    }
  } else {
    for (size_t i = 0; i < m.help_lines.size(); ++i) {
      const float top = 96.0f + 116.0f * static_cast<float>(i);
      const auto number = std::to_wstring(i + 1);
      const auto badge = D2D1::RectF(70, top + 34, 106, top + 70);
      t->FillEllipse(D2D1::Ellipse(D2D1::Point2F(88, top + 52), 18, 18), accent.Get());
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
