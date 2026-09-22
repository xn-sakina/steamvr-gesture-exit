#pragma once
#include "core/dashboard_ui.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace bailout::platform::openvr {
struct SurfaceStyle {
  float r, g, b, a;
};
struct SurfaceModel {
  std::wstring title, body, left_button, right_button, footer;
  float progress = 0;
  bool left_hover = false, right_hover = false;
};
struct DashboardSettingDisplay {
  core::DashboardSettingRow row{};
  std::wstring label, value;
};
enum class DashboardStatusTone { Available, Unavailable, Neutral };
struct DashboardStatusDisplay {
  std::wstring label, value, detail;
  DashboardStatusTone tone = DashboardStatusTone::Neutral;
};
struct DashboardSurfaceModel {
  std::wstring title, settings_title, diagnostics_title, help_title, notice;
  std::vector<DashboardSettingDisplay> settings;
  std::vector<core::DashboardButton> buttons;
  std::vector<DashboardStatusDisplay> statuses;
  std::vector<std::wstring> help_lines;
  std::optional<core::DashboardAction> hovered_action;
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
  void* texture(const std::string& id) const;
  const std::string& error() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace bailout::platform::openvr
