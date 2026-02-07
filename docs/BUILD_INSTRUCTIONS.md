# Build Instructions

## CMake
```bat
cd InputLatencyOptimizer
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## Startup behavior
- App starts **silently** (tray only).
- Settings dialog only appears when user opens it from tray / double click.
- If previously enabled, optimizer starts automatically using backed-up applied config.

## UI (slider + 2 buttons)
- **Chế độ**: slider 4 nấc: Recommend / Nhẹ / Trung Bình / Tối đa (chỉ chọn, chưa áp dụng)
- **Reset**: reset **chế độ** về **lần Apply gần nhất** (backup) / nếu chưa có backup thì về khuyến nghị
- **Apply**: compute per-device tuned config + apply + save backup

## Backup (what you changed)
When Apply is pressed, app saves full computed tuning config to registry as "AppliedConfig" so you can revert selection via Reset.
