#include "app/runtime_app.hpp"
#include "platform/windows/paths.hpp"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <sstream>

namespace bailout::app { namespace {
std::string narrow(const std::wstring& w){if(w.empty())return{};int n=WideCharToMultiByte(CP_UTF8,0,w.data(),static_cast<int>(w.size()),nullptr,0,nullptr,nullptr);std::string s(static_cast<size_t>(n),'\0');WideCharToMultiByte(CP_UTF8,0,w.data(),static_cast<int>(w.size()),s.data(),n,nullptr,nullptr);return s;}
std::wstring widen(std::string_view s){if(s.empty())return{};int n=MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0);std::wstring w(static_cast<size_t>(n),L'\0');MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),w.data(),n);return w;}
core::Vec3 cv(Vec3 v){return{v.x,v.y,v.z};}
std::wstring onoff(bool value,core::Language language){return widen(core::localize(value?core::StringId::On:core::StringId::Off,language));}
core::StringId game_reason(platform::windows::CurrentVrGameStatus status){using S=platform::windows::CurrentVrGameStatus;switch(status){case S::Available:return core::StringId::Ready;case S::ApplicationsUnavailable:return core::StringId::NoApplicationsInterface;case S::SceneNotRunning:return core::StringId::NoSceneApplication;case S::SceneProcessUnavailable:return core::StringId::SceneProcessUnavailable;case S::SelfProcessRejected:return core::StringId::SelfProcessRejected;case S::ApplicationKeyUnavailable:return core::StringId::ApplicationKeyUnavailable;case S::ApplicationMetadataUnavailable:return core::StringId::ApplicationMetadataUnavailable;case S::UnsafeApplication:return core::StringId::UnsafeApplication;}return core::StringId::Unavailable;}
core::StringId target_reason(platform::windows::ExitTargetStatus status){using S=platform::windows::ExitTargetStatus;switch(status){case S::Available:return core::StringId::Ready;case S::CurrentGameUnavailable:return core::StringId::CurrentGameUnavailable;case S::ProcessUnavailable:return core::StringId::ProcessUnavailable;case S::ProcessAccessDenied:return core::StringId::ProcessAccessDenied;case S::ProcessIdentityUnavailable:return core::StringId::ProcessIdentityUnavailable;case S::ProcessPathUnavailable:return core::StringId::ProcessPathUnavailable;case S::UnsafeProcess:return core::StringId::UnsafeProcess;}return core::StringId::Unavailable;}
}
RuntimeApp::RuntimeApp():settings_store_(platform::windows::settings_path()),log_(platform::windows::log_directory()/L"runtime.log"){
  auto loaded=settings_store_.load();settings_=loaded.settings;
  if(loaded.status!=core::SettingsLoadStatus::Loaded){
    std::string error;
    if(!settings_store_.save(settings_,&error)){
      core::DiagnosticRecord failure{};
      failure.event=core::DiagnosticEventType::Error;
      failure.code=error;
      (void)log_.append(failure);
    }
    core::DiagnosticRecord recovery{};
    recovery.event=core::DiagnosticEventType::SettingsRecovered;
    recovery.code=loaded.diagnostic_code;
    (void)log_.append(recovery);
    recovery_notice_pending_=true;
  }
  flow_=std::make_unique<core::FlowStateMachine>(std::chrono::seconds(settings_.control_timeout_seconds));
}
core::TargetIdentity RuntimeApp::core_target(const RuntimeTarget&t)const{return{t.pid,t.creation_token,narrow(t.executable_path),narrow(t.display_name)};}
core::Prerequisites RuntimeApp::prerequisites(const TrackingFrame& f,const platform::windows::TargetResolution& resolution,bool dashboard_visible)const{const auto& configured=settings_.configured_hand==core::Hand::Left?f.left:f.right;const auto& opposite=settings_.configured_hand==core::Hand::Left?f.right:f.left;core::Prerequisites p{};p.current_vr_game=resolution.game.has_value()&&!dashboard_visible;p.configured_hand_tracked=configured.tracked&&!dashboard_visible;p.opposite_pointer_available=opposite.pointer_available()&&!dashboard_visible;if(p.current_vr_game&&resolution.target)p.exit_target=core_target(*resolution.target);return p;}
std::wstring RuntimeApp::text(core::StringId id)const{return widen(core::localize(id,language_));}
bool RuntimeApp::save_setting(const core::Settings& candidate,const char* name,const std::string& value){
  std::string error;
  if(!settings_store_.save(candidate,&error)){
    (void)log_.append({core::DiagnosticEventType::Error,error});
    save_failure_notice_until_=std::chrono::steady_clock::now()+std::chrono::seconds(8);
    return false;
  }
  core::DiagnosticRecord record{};
  record.event=core::DiagnosticEventType::SettingsChanged;
  record.code="SETTINGS_SAVED";
  record.setting_name=name;
  record.setting_value=value;
  (void)log_.append(record);
  return true;
}
void RuntimeApp::log_overlay_error(std::chrono::steady_clock::time_point now){
  std::string detail=overlays_->take_error();
  if(detail.empty())detail="unknown overlay failure";
  if(detail!=last_overlay_error_||now-last_overlay_error_at_>=std::chrono::seconds(30)){
    core::DiagnosticRecord record{};
    record.event=core::DiagnosticEventType::Error;
    record.code="OVERLAY_OPERATION_FAILED";
    record.status=detail;
    (void)log_.append(record);
    last_overlay_error_=detail;
    last_overlay_error_at_=now;
  }
}
void RuntimeApp::abort_overlay_flow(std::chrono::steady_clock::time_point now,const TrackingFrame& frame){
  log_overlay_error(now);
  apply(flow_->on_overlay_failure("OVERLAY_PRESENT_FAILED"),now,frame);
}
void RuntimeApp::dashboard_input(const std::vector<core::DashboardAction>& actions){
  for(const auto action:actions){
    auto candidate=settings_;
    if(!core::apply_dashboard_action(candidate,action))continue;
    const char* name=nullptr;
    std::string value;
    if(candidate.configured_hand!=settings_.configured_hand){name="configured_hand";value=core::to_string(candidate.configured_hand);}
    else if(candidate.countdown_seconds!=settings_.countdown_seconds){name="countdown_seconds";value=std::to_string(candidate.countdown_seconds);}
    else if(candidate.control_timeout_seconds!=settings_.control_timeout_seconds){name="control_timeout_seconds";value=std::to_string(candidate.control_timeout_seconds);}
    else if(candidate.countdown_sound!=settings_.countdown_sound){name="countdown_sound";value=candidate.countdown_sound?"true":"false";}
    else if(candidate.language!=settings_.language){name="language";value=core::to_string(candidate.language);}
    if(!name||!save_setting(candidate,name,value))continue;
    const bool hand_changed=candidate.configured_hand!=settings_.configured_hand;
    const bool timeout_changed=candidate.control_timeout_seconds!=settings_.control_timeout_seconds;
    const bool language_changed=candidate.language!=settings_.language;
    settings_=candidate;
    if(hand_changed)gesture_.reset();
    if(timeout_changed&&flow_->state()==core::FlowState::NoActiveFlow)flow_=std::make_unique<core::FlowStateMachine>(std::chrono::seconds(settings_.control_timeout_seconds));
    if(language_changed){
      language_=core::resolve_language(settings_.language,steam_language_,os_language_);
      if(!overlays_->update_thumbnail(text(core::StringId::ThumbnailTitle),text(core::StringId::ThumbnailDescription),std::string(core::localize(core::StringId::AppName,language_))))log_overlay_error(std::chrono::steady_clock::now());
    }
  }
}
void RuntimeApp::update_dashboard(const platform::windows::TargetResolution& resolution,const TrackingFrame&f,bool dashboard_visible){if(!overlays_)return;const auto& configured=settings_.configured_hand==core::Hand::Left?f.left:f.right;const auto& opposite=settings_.configured_hand==core::Hand::Left?f.right:f.left;const auto now=std::chrono::steady_clock::now();if(recovery_notice_pending_&&overlays_->dashboard_visible()){recovery_notice_pending_=false;recovery_notice_until_=now+std::chrono::seconds(8);}const auto snapshot=flow_->snapshot(now);platform::openvr::DashboardSurfaceModel model{};model.title=text(core::StringId::AppName);model.settings_title=text(core::StringId::SettingsTitle);model.diagnostics_title=text(core::StringId::DiagnosticsTitle);model.help_title=text(core::StringId::QuickGuide);if(now<save_failure_notice_until_)model.notice=text(core::StringId::SettingsSaveFailed);else if(recovery_notice_pending_||now<recovery_notice_until_)model.notice=text(core::StringId::SettingsRecovered);model.settings={{core::DashboardSettingRow::ConfiguredHand,text(core::StringId::ConfiguredHand),L""},{core::DashboardSettingRow::CountdownDuration,text(core::StringId::CountdownDuration),L""},{core::DashboardSettingRow::ControlTimeout,text(core::StringId::ControlTimeout),std::to_wstring(settings_.control_timeout_seconds)+text(core::StringId::SecondsUnit)},{core::DashboardSettingRow::CountdownSound,text(core::StringId::CountdownSound),L""},{core::DashboardSettingRow::Language,text(core::StringId::LanguageLabel),L""}};model.buttons=core::dashboard_buttons(settings_);for(auto& button:model.buttons){switch(button.action){case core::DashboardAction::SetLeftHand:button.label=text(core::StringId::Left);break;case core::DashboardAction::SetRightHand:button.label=text(core::StringId::Right);break;case core::DashboardAction::SetCountdown1:button.label=L"1"+text(core::StringId::SecondsUnit);break;case core::DashboardAction::SetCountdown3:button.label=L"3"+text(core::StringId::SecondsUnit);break;case core::DashboardAction::SetCountdown5:button.label=L"5"+text(core::StringId::SecondsUnit);break;case core::DashboardAction::SetCountdown10:button.label=L"10"+text(core::StringId::SecondsUnit);break;case core::DashboardAction::SetCountdown15:button.label=L"15"+text(core::StringId::SecondsUnit);break;case core::DashboardAction::DecreaseControlTimeout:button.label=L"−";break;case core::DashboardAction::IncreaseControlTimeout:button.label=L"+";break;case core::DashboardAction::EnableCountdownSound:button.label=onoff(true,language_);break;case core::DashboardAction::DisableCountdownSound:button.label=onoff(false,language_);break;case core::DashboardAction::SetLanguageAuto:button.label=text(core::StringId::AutoLanguage);break;case core::DashboardAction::SetLanguageEnglish:button.label=text(core::StringId::EnglishLanguage);break;case core::DashboardAction::SetLanguageChinese:button.label=text(core::StringId::ChineseLanguage);break;}}
 const bool game_available=resolution.game.has_value();const bool target_available=resolution.target.has_value();const std::wstring game_value=game_available?resolution.game->display_name:text(core::StringId::Unavailable);const std::wstring game_detail=game_available?(dashboard_visible?text(core::StringId::DashboardExitDisabled):text(core::StringId::Ready)):text(game_reason(resolution.game_status));const std::wstring target_value=target_available?resolution.target->display_name:text(core::StringId::Unavailable);const std::wstring target_detail=target_available?(dashboard_visible?text(core::StringId::DashboardExitDisabled):text(core::StringId::Ready)):text(target_reason(resolution.target_status));const std::wstring last=widen(core::localize_last_exit(snapshot.last_exit,language_));model.statuses={{text(core::StringId::CurrentVrGame),game_value,game_detail,game_available?platform::openvr::DashboardStatusTone::Available:platform::openvr::DashboardStatusTone::Unavailable},{text(core::StringId::ExitTarget),target_value,target_detail,target_available?platform::openvr::DashboardStatusTone::Available:platform::openvr::DashboardStatusTone::Unavailable},{text(core::StringId::HandTracking),configured.tracked?text(core::StringId::Available):text(core::StringId::Unavailable),L"",configured.tracked?platform::openvr::DashboardStatusTone::Available:platform::openvr::DashboardStatusTone::Unavailable},{text(core::StringId::OppositePointer),opposite.pointer_available()?text(core::StringId::Available):text(core::StringId::Unavailable),L"",opposite.pointer_available()?platform::openvr::DashboardStatusTone::Available:platform::openvr::DashboardStatusTone::Unavailable},{text(core::StringId::LastExit),last,L"",platform::openvr::DashboardStatusTone::Neutral}};model.help_lines={text(core::StringId::HelpGesture),text(core::StringId::HelpOppositeHand),text(core::StringId::HelpForceClose)};if(!overlays_->update_dashboard(model))log_overlay_error(now);}
void RuntimeApp::apply(core::Effects effects,std::chrono::steady_clock::time_point now,const TrackingFrame& f){
 for(size_t i=0;i<effects.size();++i){
  const auto& e=effects[i];
  auto append=[&](core::Effects more){effects.insert(effects.end(),std::make_move_iterator(more.begin()),std::make_move_iterator(more.end()));};
  const auto& hand=settings_.configured_hand==core::Hand::Left?f.left:f.right;
  switch(e.type){
   case core::EffectType::ShowIdleControl:
    if(!overlays_->show_wrist(hand,f.sampled_at,{text(core::StringId::AppName),text(core::StringId::ExitGame),text(core::StringId::ExitGame),text(core::StringId::Dismiss)})){abort_overlay_flow(now,f);return;}
    break;
   case core::EffectType::ShowConfirmation:{
    std::wstring name=latest_resolution_.target?latest_resolution_.target->display_name:text(core::StringId::UnknownGame);
    if(!overlays_->show_wrist(hand,f.sampled_at,{text(core::StringId::ConfirmTitle),name+L"\n"+text(core::StringId::ConfirmBody),text(core::StringId::Confirm),text(core::StringId::Dismiss)})){abort_overlay_flow(now,f);return;}
    break;
   }
   case core::EffectType::HideControl:
    if(!overlays_->hide_wrist())log_overlay_error(now);
    break;
   case core::EffectType::ShowCountdownHud:
   case core::EffectType::UpdateCountdownHud:{
    double seconds=std::max(0.0,e.remaining.count()/1000.0);
    std::wstringstream value;
    value<<static_cast<int>(std::ceil(seconds))<<text(core::StringId::SecondsUnit);
    if(!overlays_->show_hud({text(core::StringId::ExitGame),value.str(),L"",L"",L"",static_cast<float>(e.progress)})){abort_overlay_flow(now,f);return;}
    if(hand.tracked&&!overlays_->show_wrist(hand,f.sampled_at,{text(core::StringId::ExitGame),value.str(),L"",text(core::StringId::CancelExit)})){abort_overlay_flow(now,f);return;}
    int beep=static_cast<int>(std::ceil(seconds));
    if(beep!=last_beep_second_&&beep>0){sound_.tick(settings_.countdown_sound);last_beep_second_=beep;}
    break;
   }
   case core::EffectType::ShowExitingHud:
    if(!overlays_->hide_wrist()){abort_overlay_flow(now,f);return;}
    if(!overlays_->show_hud({text(core::StringId::Exiting),text(core::StringId::PleaseWait)})){abort_overlay_flow(now,f);return;}
    break;
   case core::EffectType::ShowFailureHud:
    if(!overlays_->show_hud({text(core::StringId::ExitFailed),text(core::StringId::CloseManually)}))log_overlay_error(now);
    break;
   case core::EffectType::HideHud:
    if(!overlays_->hide_hud())log_overlay_error(now);
    last_beep_second_=-1;
    break;
   case core::EffectType::RequestGracefulClose:{
    if(!locked_native_||!targets_->revalidate(*locked_native_)){append(flow_->on_target_status(now,core::TargetStatus::IdentityMismatch));break;}
    bool ok=process_.request_graceful_close(*locked_native_,now);
    append(flow_->on_graceful_result(now,ok?core::CloseRequestResult::Accepted:core::CloseRequestResult::Failed,"EXIT_OPEN_PROCESS_FAILED"));
    break;
   }
   case core::EffectType::RequestForceClose:{
    if(!locked_native_||!targets_->revalidate(*locked_native_)){append(flow_->on_target_status(now,core::TargetStatus::IdentityMismatch));break;}
    auto status=process_.force_close();
    if(status==platform::windows::CloseStatus::IdentityInvalid)append(flow_->on_target_status(now,core::TargetStatus::IdentityMismatch));
    else if(status==platform::windows::CloseStatus::ExitedGracefully)append(flow_->on_target_status(now,core::TargetStatus::Exited));
    else append(flow_->on_force_result(now,status==platform::windows::CloseStatus::ForceSucceeded?core::CloseRequestResult::Accepted:core::CloseRequestResult::Failed,"EXIT_TERMINATE_PROCESS_FAILED"));
    break;
   }
   case core::EffectType::Diagnostic:{
    core::DiagnosticRecord record{};
    record.event=e.code.rfind("EXIT_",0)==0?core::DiagnosticEventType::ExitResult:core::DiagnosticEventType::StateTransition;
    record.code=e.code;
    record.from_state=std::string(core::to_string(e.from_state));
    record.to_state=std::string(core::to_string(e.to_state));
    if(e.target)record.target_name=e.target->display_name;
    (void)log_.append(record);
    break;
   }
  }
 }
}
int RuntimeApp::run(){
 if(!vr_.initialize()){log_.append({core::DiagnosticEventType::Error,"OPENVR_INIT_FAILED"});return 1;}
 overlays_=std::make_unique<platform::openvr::OverlayManager>(vr_.overlays());
 if(!overlays_->initialize()){core::DiagnosticRecord record{};record.event=core::DiagnosticEventType::Error;record.code="OVERLAY_INIT_FAILED";record.status=overlays_->error();(void)log_.append(record);vr_.shutdown();return 1;}
 targets_=std::make_unique<platform::windows::ExitTargetResolver>(vr_.applications());
 std::array<char,64> steam{};vr::EVRSettingsError language_error{};
 if(vr::VRSettings()){vr::VRSettings()->GetString("steamvr","language",steam.data(),static_cast<uint32_t>(steam.size()),&language_error);if(language_error!=vr::VRSettingsError_None)vr::VRSettings()->GetString("steam","language",steam.data(),static_cast<uint32_t>(steam.size()),&language_error);}
 if(language_error==vr::VRSettingsError_None)steam_language_=steam.data();
 std::array<wchar_t,LOCALE_NAME_MAX_LENGTH> locale{};if(GetUserDefaultLocaleName(locale.data(),static_cast<int>(locale.size())))os_language_=narrow(locale.data());
 language_=core::resolve_language(settings_.language,steam_language_,os_language_);if(!overlays_->update_thumbnail(text(core::StringId::ThumbnailTitle),text(core::StringId::ThumbnailDescription),std::string(core::localize(core::StringId::AppName,language_))))log_overlay_error(std::chrono::steady_clock::now());log_.append({core::DiagnosticEventType::Lifecycle,"RUNTIME_STARTED"});
 auto next_target=std::chrono::steady_clock::now();
 while(!stop_){
  vr::VREvent_t system_event{};while(vr_.system()->PollNextEvent(&system_event,sizeof(system_event))){if(system_event.eventType==vr::VREvent_Quit){vr_.system()->AcknowledgeQuit_Exiting();stop_=true;}}
  if(stop_)break;
  const auto now=std::chrono::steady_clock::now();const auto frame=vr_.sample_tracking();const bool dashboard_visible=vr_.overlays()->IsDashboardVisible();
  if(now>=next_target){latest_resolution_=targets_->resolve_foreground();next_target=now+std::chrono::milliseconds(250);}
  const auto prereq=prerequisites(frame,latest_resolution_,dashboard_visible);
  std::string availability="game="+std::string(platform::windows::diagnostic_code(latest_resolution_.game_status))+";target="+platform::windows::diagnostic_code(latest_resolution_.target_status)+";hand="+(prereq.configured_hand_tracked?"1":"0")+";pointer="+(prereq.opposite_pointer_available?"1":"0")+";dashboard="+(dashboard_visible?"1":"0");
  if(availability!=availability_signature_){availability_signature_=availability;core::DiagnosticRecord r{};r.event=core::DiagnosticEventType::AvailabilityChanged;r.code="RUNTIME_PREREQUISITES_CHANGED";r.status=availability;r.current_game_status=platform::windows::diagnostic_code(latest_resolution_.game_status);r.exit_target_status=platform::windows::diagnostic_code(latest_resolution_.target_status);log_.append(r);}
  apply(flow_->on_prerequisites(now,prereq),now,frame);
  const auto& configured=settings_.configured_hand==core::Hand::Left?frame.left:frame.right;const auto& opposite=settings_.configured_hand==core::Hand::Left?frame.right:frame.left;const Vec3 palm=settings_.configured_hand==core::Hand::Left?Vec3{-configured.right.x,-configured.right.y,-configured.right.z}:configured.right;
  bool gesture=false;if(dashboard_visible){gesture_.reset();if(flow_->state()==core::FlowState::CountingDown&&!overlays_->hide_wrist())log_overlay_error(now);}else gesture=gesture_.update({now,configured.tracked&&frame.headset_tracked,cv(configured.position),cv(palm),cv(frame.headset_position),cv(frame.headset_forward)});
  if(gesture&&!vr_.overlays()->IsDashboardVisible()){log_.append({core::DiagnosticEventType::GestureRecognized,"GESTURE_RECOGNIZED"});apply(flow_->on_gesture(now,prereq),now,frame);}
  const auto state=flow_->state();
  if(!dashboard_visible){
   const auto actions=overlays_->poll_wrist_events(opposite,state==core::FlowState::Confirming,state==core::FlowState::CountingDown);
   if(!overlays_->error().empty())abort_overlay_flow(now,frame);
   else for(const auto action:actions){
   if(vr_.overlays()->IsDashboardVisible())break;
   if(action==platform::openvr::OverlayAction::Primary)apply(flow_->on_control_clicked(now),now,frame);
   else if(action==platform::openvr::OverlayAction::Dismiss)apply(flow_->on_dismiss(),now,frame);
   else if(action==platform::openvr::OverlayAction::Cancel)apply(flow_->on_cancel(),now,frame);
   else if(action==platform::openvr::OverlayAction::Confirm){
    const bool dashboard_now=vr_.overlays()->IsDashboardVisible();platform::windows::TargetResolution fresh{};if(!dashboard_now)fresh=targets_->resolve_foreground();
    apply(flow_->on_prerequisites(now,prerequisites(frame,fresh,dashboard_now)),now,frame);
    if(!dashboard_now&&fresh.target&&latest_resolution_.target&&core_target(*fresh.target)==core_target(*latest_resolution_.target)&&flow_->state()==core::FlowState::Confirming){locked_native_=fresh.target;latest_resolution_=fresh;apply(flow_->on_confirm(now,core_target(*fresh.target),std::chrono::seconds(settings_.countdown_seconds)),now,frame);}
   }
   }
  }
  const auto process_status=process_.poll(now);if(flow_->state()==core::FlowState::Exiting&&(process_status==platform::windows::CloseStatus::ExitedGracefully||process_status==platform::windows::CloseStatus::IdentityInvalid))apply(flow_->on_target_status(now,process_status==platform::windows::CloseStatus::ExitedGracefully?core::TargetStatus::Exited:core::TargetStatus::IdentityMismatch),now,frame);
  apply(flow_->tick(now),now,frame);
  if(dashboard_visible){if(!overlays_->hide_wrist())log_overlay_error(now);}
  else if(overlays_->wrist_visible()){
   if(!configured.tracked){if(!overlays_->hide_wrist())log_overlay_error(now);}
   else if(!overlays_->update_wrist_pose(configured,frame.sampled_at))abort_overlay_flow(now,frame);
  }
  const auto dashboard_actions=overlays_->poll_dashboard_events();
  if(!overlays_->error().empty())log_overlay_error(now);
  else dashboard_input(dashboard_actions);
  update_dashboard(latest_resolution_,frame,dashboard_visible);
  Sleep(11);
 }
 process_.clear();overlays_->shutdown();vr_.shutdown();log_.append({core::DiagnosticEventType::Lifecycle,"RUNTIME_STOPPED"});return 0;
}
}
