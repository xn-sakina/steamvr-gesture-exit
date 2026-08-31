#pragma once
#include <cstdint>
#include <memory>
#include <string>
namespace bailout::platform::openvr {
struct SurfaceStyle {float r,g,b,a;};
struct SurfaceModel {
  std::wstring title,body,left_button,right_button,footer;
  float progress=0; bool left_hover=false,right_hover=false;
};
class D3dOverlayRenderer {
 public:
  D3dOverlayRenderer();~D3dOverlayRenderer();
  bool initialize();
  bool create_surface(const std::string& id,uint32_t width,uint32_t height);
  bool render(const std::string& id,const SurfaceModel& model);
  void* texture(const std::string& id)const;
  const std::string& error()const;
 private: struct Impl;std::unique_ptr<Impl> impl_;
};
}
