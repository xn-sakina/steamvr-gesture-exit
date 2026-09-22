#include "platform/openvr/overlay_manager.hpp"
#include <array>
namespace bailout::platform::openvr { namespace {
constexpr char kDashboardKey[]="com.steamvr-bail-out.dashboard";constexpr char kWristKey[]="com.steamvr-bail-out.wrist";constexpr char kHudKey[]="com.steamvr-bail-out.hud";
vr::HmdMatrix34_t hud_transform(){vr::HmdMatrix34_t m={{{1,0,0,0.30f},{0,1,0,0.17f},{0,0,1,-0.70f}}};return m;}
}
bool OverlayManager::initialize(){if(!overlays_){error_="IVROverlay unavailable";return false;}if(!renderer_.initialize()){error_=renderer_.error();return false;}if(!renderer_.create_surface("dashboard",1024,640)||!renderer_.create_surface("thumbnail",256,256)||!renderer_.create_surface("wrist",768,384)||!renderer_.create_surface("hud",768,256)){error_="overlay texture creation failed";return false;}
 if(overlays_->CreateDashboardOverlay(kDashboardKey,"SteamVR Bail Out",&dashboard_,&thumbnail_)!=vr::VROverlayError_None||overlays_->CreateOverlay(kWristKey,"Bail Out Wrist",&wrist_)!=vr::VROverlayError_None||overlays_->CreateOverlay(kHudKey,"Bail Out HUD",&hud_)!=vr::VROverlayError_None){error_="OpenVR overlay creation failed";shutdown();return false;}
 // Every renderer surface uses premultiplied alpha, including the thumbnail.
 for(const auto handle:{dashboard_,thumbnail_,wrist_,hud_}){
  if(overlays_->SetOverlayFlag(handle,vr::VROverlayFlags_IsPremultiplied,true)!=vr::VROverlayError_None){
   error_="OpenVR premultiplied alpha setup failed";
   shutdown();
   return false;
  }
 }
 overlays_->SetOverlayWidthInMeters(dashboard_,1.5f);overlays_->SetOverlayInputMethod(dashboard_,vr::VROverlayInputMethod_Mouse);vr::HmdVector2_t dashboard_scale{{1024,640}};overlays_->SetOverlayMouseScale(dashboard_,&dashboard_scale);
 overlays_->SetOverlayWidthInMeters(wrist_,0.27f);overlays_->SetOverlayInputMethod(wrist_,vr::VROverlayInputMethod_Mouse);vr::HmdVector2_t wrist_scale{{768,384}};overlays_->SetOverlayMouseScale(wrist_,&wrist_scale);overlays_->SetOverlayFlag(wrist_,vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible,true);overlays_->SetOverlayFlag(wrist_,vr::VROverlayFlags_EnableClickStabilization,true);overlays_->SetOverlayFlag(wrist_,vr::VROverlayFlags_MultiCursor,true);
 overlays_->SetOverlayWidthInMeters(hud_,0.42f);overlays_->SetOverlayInputMethod(hud_,vr::VROverlayInputMethod_None);auto hm=hud_transform();overlays_->SetOverlayTransformTrackedDeviceRelative(hud_,vr::k_unTrackedDeviceIndex_Hmd,&hm);
 return true;}
void OverlayManager::update_thumbnail(const std::wstring& title,const std::wstring& description,const std::string& friendly_name){
 if(dashboard_!=vr::k_ulOverlayHandleInvalid)overlays_->SetOverlayName(dashboard_,friendly_name.c_str());
 SurfaceModel thumb{title,description,L"",L"",L"",0,false,false};submit(thumbnail_,"thumbnail",thumb);
}
void OverlayManager::shutdown(){if(!overlays_)return;for(auto h:{wrist_,hud_,dashboard_})if(h!=vr::k_ulOverlayHandleInvalid)overlays_->DestroyOverlay(h);wrist_=hud_=dashboard_=thumbnail_=vr::k_ulOverlayHandleInvalid;wrist_visible_=false;wrist_device_=vr::k_unTrackedDeviceIndexInvalid;wrist_smoother_.reset();}
bool OverlayManager::submit(vr::VROverlayHandle_t h,const std::string&id,const SurfaceModel&m){if(h==vr::k_ulOverlayHandleInvalid||!renderer_.render(id,m))return false;vr::Texture_t t{renderer_.texture(id),vr::TextureType_DirectX,vr::ColorSpace_Auto};return overlays_->SetOverlayTexture(h,&t)==vr::VROverlayError_None;}
void OverlayManager::show_wrist(const app::HandPose& hand,core::TimePoint sampled_at,const SurfaceModel&m){if(!hand.tracked||hand.device_index==vr::k_unTrackedDeviceIndexInvalid)return;bool same_content=wrist_model_.title==m.title&&wrist_model_.body==m.body&&wrist_model_.left_button==m.left_button&&wrist_model_.right_button==m.right_button&&wrist_model_.progress==m.progress;SurfaceModel next=m;if(same_content){next.left_hover=wrist_model_.left_hover;next.right_hover=wrist_model_.right_hover;}wrist_model_=std::move(next);if(!same_content)submit(wrist_,"wrist",wrist_model_);if(wrist_device_!=hand.device_index){wrist_smoother_.reset();wrist_device_=hand.device_index;}update_wrist_pose(hand,sampled_at);if(wrist_device_!=vr::k_unTrackedDeviceIndexInvalid&&overlays_->ShowOverlay(wrist_)==vr::VROverlayError_None)wrist_visible_=true;}
void OverlayManager::update_wrist_pose(const app::HandPose& hand,core::TimePoint sampled_at){
 if(wrist_device_==vr::k_unTrackedDeviceIndexInvalid)return;
 if(!hand.tracked||hand.device_index!=wrist_device_){hide_wrist();return;}
 const core::Vec3 right{hand.right.x,hand.right.y,hand.right.z};
 const core::Vec3 up{hand.up.x,hand.up.y,hand.up.z};
 const core::Vec3 back{-hand.forward.x,-hand.forward.y,-hand.forward.z};
 const core::Vec3 position{hand.position.x-0.055*up.x-0.115*back.x,
                           hand.position.y-0.055*up.y-0.115*back.y,
                           hand.position.z-0.055*up.z-0.115*back.z};
 const auto pose=wrist_smoother_.update({position,right,up,back},sampled_at);
 vr::HmdMatrix34_t transform={{{static_cast<float>(pose.right.x),static_cast<float>(pose.up.x),static_cast<float>(pose.back.x),static_cast<float>(pose.position.x)},
                               {static_cast<float>(pose.right.y),static_cast<float>(pose.up.y),static_cast<float>(pose.back.y),static_cast<float>(pose.position.y)},
                               {static_cast<float>(pose.right.z),static_cast<float>(pose.up.z),static_cast<float>(pose.back.z),static_cast<float>(pose.position.z)}}};
 if(overlays_->SetOverlayTransformAbsolute(wrist_,vr::TrackingUniverseStanding,&transform)!=vr::VROverlayError_None)hide_wrist();
}
void OverlayManager::hide_wrist(){if(wrist_!=vr::k_ulOverlayHandleInvalid)overlays_->HideOverlay(wrist_);wrist_visible_=false;wrist_device_=vr::k_unTrackedDeviceIndexInvalid;wrist_smoother_.reset();}
void OverlayManager::show_hud(const SurfaceModel&m){submit(hud_,"hud",m);overlays_->ShowOverlay(hud_);}void OverlayManager::hide_hud(){if(hud_!=vr::k_ulOverlayHandleInvalid)overlays_->HideOverlay(hud_);}void OverlayManager::update_dashboard(const DashboardSurfaceModel&m){auto next=m;next.hovered_action=dashboard_model_.hovered_action;dashboard_model_=std::move(next);if(dashboard_!=vr::k_ulOverlayHandleInvalid&&renderer_.render_dashboard("dashboard",dashboard_model_)){vr::Texture_t t{renderer_.texture("dashboard"),vr::TextureType_DirectX,vr::ColorSpace_Auto};overlays_->SetOverlayTexture(dashboard_,&t);}}
std::vector<OverlayAction> OverlayManager::poll_wrist_events(const app::HandPose& hand,bool confirming,bool counting){std::vector<OverlayAction> out;bool left=wrist_model_.left_hover,right=wrist_model_.right_hover;vr::VREvent_t event{};while(overlays_->PollNextOverlayEvent(wrist_,&event,sizeof(event))){if(event.eventType==vr::VREvent_FocusLeave){left=false;right=false;continue;}if(!wrist_visible_||!hand.accepts_pointer_event(event.trackedDeviceIndex))continue;if(event.eventType==vr::VREvent_MouseMove){const float x=event.data.mouse.x,y=384.0f-event.data.mouse.y;left=y>=272&&y<=330&&x>=42&&x<=372;right=y>=272&&y<=330&&x>=396&&x<=726;}else if(event.eventType==vr::VREvent_MouseButtonDown&&event.data.mouse.button==vr::VRMouseButton_Left){if(counting){if(right)out.push_back(OverlayAction::Cancel);}else if(confirming){if(left)out.push_back(OverlayAction::Confirm);else if(right)out.push_back(OverlayAction::Dismiss);}else{if(left)out.push_back(OverlayAction::Primary);else if(right)out.push_back(OverlayAction::Dismiss);}}}if(!wrist_visible_||!hand.pointer_available()){left=false;right=false;}if(wrist_model_.left_hover!=left||wrist_model_.right_hover!=right){wrist_model_.left_hover=left;wrist_model_.right_hover=right;submit(wrist_,"wrist",wrist_model_);}return out;}
std::vector<core::DashboardAction> OverlayManager::poll_dashboard_events(){std::vector<core::DashboardAction>out;auto hovered=dashboard_model_.hovered_action;vr::VREvent_t e{};while(overlays_->PollNextOverlayEvent(dashboard_,&e,sizeof(e))){if(e.eventType==vr::VREvent_FocusLeave){hovered.reset();continue;}if(e.eventType!=vr::VREvent_MouseMove&&e.eventType!=vr::VREvent_MouseButtonDown)continue;const float x=e.data.mouse.x,y=640.0f-e.data.mouse.y;const auto hit=core::dashboard_hit_test(dashboard_model_.buttons,x,y);if(e.eventType==vr::VREvent_MouseMove)hovered=hit;else if(e.data.mouse.button==vr::VRMouseButton_Left&&hit)out.push_back(*hit);}if(hovered!=dashboard_model_.hovered_action){dashboard_model_.hovered_action=hovered;if(renderer_.render_dashboard("dashboard",dashboard_model_)){vr::Texture_t t{renderer_.texture("dashboard"),vr::TextureType_DirectX,vr::ColorSpace_Auto};overlays_->SetOverlayTexture(dashboard_,&t);}}return out;}
}
