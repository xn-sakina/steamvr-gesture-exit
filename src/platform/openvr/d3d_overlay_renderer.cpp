#include "platform/openvr/d3d_overlay_renderer.hpp"
#include <windows.h>
#include <d3d11.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <algorithm>
#include <map>
namespace bailout::platform::openvr { using Microsoft::WRL::ComPtr;
struct D3dOverlayRenderer::Impl{
 struct Surface{uint32_t w=0,h=0;ComPtr<ID3D11Texture2D> texture;ComPtr<ID2D1RenderTarget> target;};
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;ComPtr<ID2D1Factory> d2d;ComPtr<IDWriteFactory> write;ComPtr<IDWriteTextFormat> title,body,button,section,label,small;std::map<std::string,Surface> surfaces;std::string error;
 ComPtr<ID2D1SolidColorBrush> brush(ID2D1RenderTarget* t,D2D1_COLOR_F c){ComPtr<ID2D1SolidColorBrush>b;if(FAILED(t->CreateSolidColorBrush(c,&b)))error="CreateSolidColorBrush failed";return b;}
};
D3dOverlayRenderer::D3dOverlayRenderer():impl_(std::make_unique<Impl>()){}D3dOverlayRenderer::~D3dOverlayRenderer()=default;
bool D3dOverlayRenderer::initialize(){UINT flags=D3D11_CREATE_DEVICE_BGRA_SUPPORT;D3D_FEATURE_LEVEL got{};constexpr D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1};HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,flags,levels,3,D3D11_SDK_VERSION,&impl_->device,&got,&impl_->context);if(FAILED(hr))hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,flags,levels,3,D3D11_SDK_VERSION,&impl_->device,&got,&impl_->context);if(FAILED(hr)){impl_->error="D3D11CreateDevice failed";return false;}if(FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,impl_->d2d.GetAddressOf()))||FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(impl_->write.GetAddressOf())))){impl_->error="Direct2D/DirectWrite initialization failed";return false;}impl_->write->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_SEMI_BOLD,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,46,L"",&impl_->title);impl_->write->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,27,L"",&impl_->body);impl_->write->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_SEMI_BOLD,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,30,L"",&impl_->button);impl_->write->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_SEMI_BOLD,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,24,L"",&impl_->section);impl_->write->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,20,L"",&impl_->label);impl_->write->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,16,L"",&impl_->small);if(!impl_->title||!impl_->body||!impl_->button||!impl_->section||!impl_->label||!impl_->small){impl_->error="CreateTextFormat failed";return false;}impl_->button->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);impl_->button->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);return true;}
bool D3dOverlayRenderer::create_surface(const std::string& id,uint32_t w,uint32_t h){impl_->error.clear();D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;Impl::Surface s{w,h};if(FAILED(impl_->device->CreateTexture2D(&d,nullptr,&s.texture))){impl_->error="CreateTexture2D failed";return false;}ComPtr<IDXGISurface> dxgi;if(FAILED(s.texture.As(&dxgi))){impl_->error="IDXGISurface query failed";return false;}auto props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));if(FAILED(impl_->d2d->CreateDxgiSurfaceRenderTarget(dxgi.Get(),&props,&s.target))){impl_->error="CreateDxgiSurfaceRenderTarget failed";return false;}impl_->surfaces[id]=std::move(s);return true;}
bool D3dOverlayRenderer::render(const std::string& id,const SurfaceModel& m){impl_->error.clear();auto it=impl_->surfaces.find(id);if(it==impl_->surfaces.end()){impl_->error="surface unavailable";return false;}auto& s=it->second;auto*t=s.target.Get();auto white=impl_->brush(t,D2D1::ColorF(0.96f,0.98f,1,1));auto muted=impl_->brush(t,D2D1::ColorF(0.69f,0.74f,0.82f,1));auto accent=impl_->brush(t,D2D1::ColorF(0.15f,0.68f,0.88f,1));auto panel=impl_->brush(t,D2D1::ColorF(0.035f,0.055f,0.085f,0.94f));auto hover=impl_->brush(t,D2D1::ColorF(0.12f,0.34f,0.45f,1));auto normal=impl_->brush(t,D2D1::ColorF(0.08f,0.15f,0.22f,1));if(!white||!muted||!accent||!panel||!hover||!normal)return false;t->BeginDraw();t->Clear(D2D1::ColorF(0,0,0,0));t->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(8,8,float(s.w-8),float(s.h-8)),24,24),panel.Get());t->DrawTextW(m.title.c_str(),static_cast<UINT32>(m.title.size()),impl_->title.Get(),D2D1::RectF(42,30,float(s.w-42),100),white.Get());t->DrawTextW(m.body.c_str(),static_cast<UINT32>(m.body.size()),impl_->body.Get(),D2D1::RectF(42,105,float(s.w-42),float(s.h-105)),muted.Get());if(m.progress>0){float right=42+(s.w-84)*std::clamp(m.progress,0.0f,1.0f);t->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(42,float(s.h-38),right,float(s.h-24)),7,7),accent.Get());}if(!m.left_button.empty()){auto r=D2D1::RoundedRect(D2D1::RectF(42,float(s.h-112),float(s.w/2-12),float(s.h-54)),12,12);t->FillRoundedRectangle(r,m.left_hover?hover.Get():normal.Get());t->DrawTextW(m.left_button.c_str(),static_cast<UINT32>(m.left_button.size()),impl_->button.Get(),r.rect,white.Get());}if(!m.right_button.empty()){auto r=D2D1::RoundedRect(D2D1::RectF(float(s.w/2+12),float(s.h-112),float(s.w-42),float(s.h-54)),12,12);t->FillRoundedRectangle(r,m.right_hover?hover.Get():normal.Get());t->DrawTextW(m.right_button.c_str(),static_cast<UINT32>(m.right_button.size()),impl_->button.Get(),r.rect,white.Get());}auto result=t->EndDraw();if(FAILED(result))impl_->error="EndDraw failed";return SUCCEEDED(result);}
bool D3dOverlayRenderer::render_thumbnail(const std::string& id,
                                          const std::wstring& title,
                                          const std::wstring& description) {
 auto it=impl_->surfaces.find(id);
 impl_->error.clear();if(it==impl_->surfaces.end()||it->second.w!=256||it->second.h!=256){impl_->error="thumbnail surface unavailable";return false;}
 auto* target=it->second.target.Get();
 auto panel=impl_->brush(target,D2D1::ColorF(0x061322,0.97f));
 auto badge=impl_->brush(target,D2D1::ColorF(0x061322,1.0f));
 auto outline=impl_->brush(target,D2D1::ColorF(0x24475e,1.0f));
 auto cyan=impl_->brush(target,D2D1::ColorF(0x27b6df,1.0f));
 auto white=impl_->brush(target,D2D1::ColorF(0xf5fbff,1.0f));
 auto muted=impl_->brush(target,D2D1::ColorF(0xaabccd,1.0f));
 ComPtr<ID2D1PathGeometry> ring;
 ComPtr<ID2D1GeometrySink> ring_sink;
 ComPtr<ID2D1StrokeStyle> round_stroke;
 ComPtr<IDWriteTextFormat> title_format,description_format;
 const auto stroke_props=D2D1::StrokeStyleProperties(
     D2D1_CAP_STYLE_ROUND,D2D1_CAP_STYLE_ROUND,D2D1_CAP_STYLE_ROUND,
     D2D1_LINE_JOIN_ROUND);
 if(!panel||!badge||!outline||!cyan||!white||!muted||
    FAILED(impl_->d2d->CreatePathGeometry(&ring))||
    FAILED(ring->Open(&ring_sink))||
    FAILED(impl_->d2d->CreateStrokeStyle(stroke_props,nullptr,0,&round_stroke))||
    FAILED(impl_->write->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,27,L"",&title_format))||
    FAILED(impl_->write->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,16,L"",&description_format)))return false;
 ring_sink->BeginFigure(D2D1::Point2F(184,75),D2D1_FIGURE_BEGIN_HOLLOW);
 ring_sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(184,181),D2D1::SizeF(78,78),
     0.0f,D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,D2D1_ARC_SIZE_LARGE));
 ring_sink->EndFigure(D2D1_FIGURE_END_OPEN);
 if(FAILED(ring_sink->Close()))return false;
 title_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
 title_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
 description_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
 description_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
 target->BeginDraw();
 target->Clear(D2D1::ColorF(0,0,0,0));
 target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(8,8,248,248),24,24),panel.Get());
 D2D1_MATRIX_3X2_F previous;
 target->GetTransform(&previous);
 target->SetTransform(D2D1::Matrix3x2F::Scale(0.45f,0.45f)*
                      D2D1::Matrix3x2F::Translation(70.4f,23.4f));
 target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(8,8,248,248),52,52),badge.Get());
 target->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(10,10,246,246),50,50),outline.Get(),4);
 target->DrawGeometry(ring.Get(),cyan.Get(),18,round_stroke.Get());
 target->DrawLine(D2D1::Point2F(91,128),D2D1::Point2F(174,128),white.Get(),18,round_stroke.Get());
 target->DrawLine(D2D1::Point2F(145,99),D2D1::Point2F(174,128),white.Get(),18,round_stroke.Get());
 target->DrawLine(D2D1::Point2F(174,128),D2D1::Point2F(145,157),white.Get(),18,round_stroke.Get());
 target->SetTransform(previous);
 target->DrawTextW(title.c_str(),static_cast<UINT32>(title.size()),title_format.Get(),
                   D2D1::RectF(14,153,242,194),white.Get());
 target->DrawTextW(description.c_str(),static_cast<UINT32>(description.size()),
                   description_format.Get(),D2D1::RectF(14,200,242,235),muted.Get());
 auto result=target->EndDraw();if(FAILED(result))impl_->error="EndDraw(thumbnail) failed";return SUCCEEDED(result);
}
bool D3dOverlayRenderer::render_dashboard(const std::string& id,const DashboardSurfaceModel& m){impl_->error.clear();auto it=impl_->surfaces.find(id);if(it==impl_->surfaces.end())return false;auto& s=it->second;auto*t=s.target.Get();auto white=impl_->brush(t,D2D1::ColorF(0.96f,0.98f,1,1));auto muted=impl_->brush(t,D2D1::ColorF(0.66f,0.72f,0.81f,1));auto faint=impl_->brush(t,D2D1::ColorF(0.38f,0.44f,0.52f,1));auto accent=impl_->brush(t,D2D1::ColorF(0.10f,0.58f,0.82f,1));auto hover=impl_->brush(t,D2D1::ColorF(0.20f,0.74f,0.94f,1));auto normal=impl_->brush(t,D2D1::ColorF(0.075f,0.12f,0.18f,1));auto card=impl_->brush(t,D2D1::ColorF(0.055f,0.085f,0.13f,1));auto panel=impl_->brush(t,D2D1::ColorF(0.025f,0.04f,0.065f,0.97f));auto good=impl_->brush(t,D2D1::ColorF(0.35f,0.86f,0.62f,1));auto bad=impl_->brush(t,D2D1::ColorF(0.98f,0.55f,0.38f,1));if(!white||!muted||!faint||!accent||!hover||!normal||!card||!panel||!good||!bad)return false;t->BeginDraw();t->Clear(D2D1::ColorF(0,0,0,0));t->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(8,8,float(s.w-8),float(s.h-8)),24,24),panel.Get());t->DrawTextW(m.title.c_str(),static_cast<UINT32>(m.title.size()),impl_->title.Get(),D2D1::RectF(36,20,620,75),white.Get());if(!m.notice.empty())t->DrawTextW(m.notice.c_str(),static_cast<UINT32>(m.notice.size()),impl_->small.Get(),D2D1::RectF(620,30,988,70),bad.Get());t->DrawTextW(m.settings_title.c_str(),static_cast<UINT32>(m.settings_title.size()),impl_->section.Get(),D2D1::RectF(42,78,594,110),white.Get());t->DrawTextW(m.diagnostics_title.c_str(),static_cast<UINT32>(m.diagnostics_title.size()),impl_->section.Get(),D2D1::RectF(626,78,982,110),white.Get());for(size_t i=0;i<m.settings.size();++i){const float top=116.0f+62.0f*static_cast<float>(i);const auto&r=m.settings[i];t->DrawTextW(r.label.c_str(),static_cast<UINT32>(r.label.size()),impl_->label.Get(),D2D1::RectF(42,top+16,216,top+52),muted.Get());if(!r.value.empty()){auto value_rect=D2D1::RectF(351,top+8,467,top+54);t->FillRoundedRectangle(D2D1::RoundedRect(value_rect,9,9),normal.Get());t->DrawTextW(r.value.c_str(),static_cast<UINT32>(r.value.size()),impl_->button.Get(),value_rect,white.Get());}}for(const auto& b:m.buttons){auto rect=D2D1::RectF(b.bounds.left,b.bounds.top,b.bounds.right,b.bounds.bottom);const bool hovered=m.hovered_action&&*m.hovered_action==b.action;t->FillRoundedRectangle(D2D1::RoundedRect(rect,9,9),(b.selected?accent.Get():normal.Get()));if(hovered)t->DrawRoundedRectangle(D2D1::RoundedRect(rect,9,9),hover.Get(),3.0f);else if(b.selected)t->DrawRoundedRectangle(D2D1::RoundedRect(rect,9,9),white.Get(),1.5f);auto* text_brush=b.enabled?white.Get():faint.Get();t->DrawTextW(b.label.c_str(),static_cast<UINT32>(b.label.size()),impl_->button.Get(),rect,text_brush);}for(size_t i=0;i<m.statuses.size();++i){const float top=112.0f+62.0f*static_cast<float>(i);const auto& status=m.statuses[i];auto rect=D2D1::RectF(620,top,988,top+58);t->FillRoundedRectangle(D2D1::RoundedRect(rect,8,8),card.Get());t->DrawTextW(status.label.c_str(),static_cast<UINT32>(status.label.size()),impl_->small.Get(),D2D1::RectF(632,top+4,790,top+25),muted.Get());auto*tone=status.tone==DashboardStatusTone::Available?good.Get():status.tone==DashboardStatusTone::Unavailable?bad.Get():white.Get();t->DrawTextW(status.value.c_str(),static_cast<UINT32>(status.value.size()),impl_->small.Get(),D2D1::RectF(792,top+4,976,top+25),tone);t->DrawTextW(status.detail.c_str(),static_cast<UINT32>(status.detail.size()),impl_->small.Get(),D2D1::RectF(632,top+25,976,top+57),white.Get());}t->DrawTextW(m.help_title.c_str(),static_cast<UINT32>(m.help_title.size()),impl_->section.Get(),D2D1::RectF(42,442,980,475),white.Get());for(size_t i=0;i<m.help_lines.size();++i){const float top=478.0f+38.0f*static_cast<float>(i);t->DrawTextW(m.help_lines[i].c_str(),static_cast<UINT32>(m.help_lines[i].size()),impl_->small.Get(),D2D1::RectF(48,top,978,top+34),muted.Get());}return SUCCEEDED(t->EndDraw());}
void* D3dOverlayRenderer::texture(const std::string& id)const{auto it=impl_->surfaces.find(id);return it==impl_->surfaces.end()?nullptr:it->second.texture.Get();}const std::string& D3dOverlayRenderer::error()const{return impl_->error;}
}
