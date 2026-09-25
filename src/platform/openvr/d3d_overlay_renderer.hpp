#pragma once
#include "core/dashboard_ui.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>
namespace bailout::platform::openvr {
struct SurfaceStyle {
  float r, g, b, a;
};
struct SurfaceModel {
  SurfaceModel() = default;
  SurfaceModel(std::wstring next_title, std::wstring next_body,
               std::wstring next_left_button = {}, std::wstring next_right_button = {},
               std::wstring next_footer = {}, float next_progress = 0)
      : title(std::move(next_title)),
        body(std::move(next_body)),
        left_button(std::move(next_left_button)),
        right_button(std::move(next_right_button)),
        footer(std::move(next_footer)),
        progress(next_progress) {}

  std::wstring title, body, left_button, right_button, footer;
  float progress = 0;
  bool left_hover = false, right_hover = false;
  bool pointer_visible = false;
  float pointer_x = 0, pointer_y = 0;
};
struct DashboardSettingDisplay {
  core::DashboardSettingRow row{};
  std::wstring label, value;
  bool operator==(const DashboardSettingDisplay&) const = default;
};
enum class DashboardStatusTone { Available, Unavailable, Neutral };
struct DashboardStatusDisplay {
  std::wstring label, value, detail;
  DashboardStatusTone tone = DashboardStatusTone::Neutral;
  bool operator==(const DashboardStatusDisplay&) const = default;
};
struct DashboardSurfaceModel {
  std::wstring title, settings_title, diagnostics_title, help_title, notice;
  core::DashboardTab active_tab = core::DashboardTab::Settings;
  std::optional<core::DashboardTab> hovered_tab;
  std::vector<DashboardSettingDisplay> settings;
  std::vector<core::DashboardButton> buttons;
  std::vector<DashboardStatusDisplay> statuses;
  std::vector<std::wstring> help_lines;
  std::optional<core::DashboardAction> hovered_action;
  bool operator==(const DashboardSurfaceModel&) const = default;
};
class D3dOverlayRenderer {
 public:
  D3dOverlayRenderer();
  ~D3dOverlayRenderer();
  bool initialize();
  bool create_surface(const std::string& id, uint32_t width, uint32_t height);
  bool render(const std::string& id, const SurfaceModel& model);
  bool render_thumbnail(const std::string& id, const std::wstring& title,
                        const std::wstring& description);
  bool render_dashboard(const std::string& id, const DashboardSurfaceModel& model);
  // OpenVR copies Direct3D overlay textures when SetOverlayTexture is called.
  // Flush only after that call so the copy observes the commands that produced
  // the submitted frame instead of the previous frame.
  void flush();
  void* texture(const std::string& id) const;
  uint64_t generation() const;
  bool retry_pending_recovery();
  const std::string& error() const;

 private:
  bool render(const std::string& id, const SurfaceModel& model, bool retried);
  bool render_thumbnail(const std::string& id, const std::wstring& title,
                        const std::wstring& description, bool retried);
  bool render_dashboard(const std::string& id, const DashboardSurfaceModel& model, bool retried);
  bool recover_target(const std::string& id, long result);
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace bailout::platform::openvr
