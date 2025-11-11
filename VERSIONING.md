# Version Management Guide

本项目采用 [语义化版本控制](https://semver.org/lang/zh-CN/) (Semantic Versioning)。

## 版本号格式

版本号格式：`主版本号.次版本号.修订号` (例如：`2.1.0`)

- **主版本号 (MAJOR)**: 当你做了不兼容的 API 修改
- **次版本号 (MINOR)**: 当你做了向下兼容的功能性新增
- **修订号 (PATCH)**: 当你做了向下兼容的问题修正

## 版本文件

- `VERSION`: 包含当前版本号（纯文本）
- `include/config.hpp`: 包含版本宏定义
- `CHANGELOG.md`: 详细的变更日志

## 发布流程

### 手动发布（推荐用于首次发布）

1. **更新版本号**
   ```bash
   # 编辑 VERSION 文件
   echo "2.1.0" > VERSION
   
   # 更新 config.hpp 中的版本宏
   # 更新 CHANGELOG.md
   ```

2. **构建固件**
   ```bash
   pio run -e m5stack-cardputer
   ```

3. **创建 Git 标签**
   ```bash
   git add VERSION include/config.hpp CHANGELOG.md
   git commit -m "chore: Bump version to 2.1.0"
   git tag -a "v2.1.0" -m "Release v2.1.0"
   git push origin main
   git push origin v2.1.0
   ```

4. **创建 GitHub Release**
   - 访问: https://github.com/vicliu624/CardPuter_Mp3_Adv/releases/new
   - 选择标签: `v2.1.0`
   - 标题: `Release v2.1.0`
   - 描述: 从 `CHANGELOG.md` 复制对应版本的变更说明
   - 上传文件: `.pio/build/m5stack-cardputer/firmware.bin`

### 使用发布脚本（推荐）

#### Windows (PowerShell)
```powershell
.\scripts\release.ps1 2.1.0
```

#### Linux/macOS (Bash)
```bash
chmod +x scripts/release.sh
./scripts/release.sh 2.1.0
```

脚本会自动：
1. 更新 `VERSION` 文件
2. 更新 `config.hpp` 中的版本宏
3. 构建固件
4. 创建发布目录
5. 从 `CHANGELOG.md` 生成发布说明
6. 提交更改并创建 Git 标签

### 自动化发布（GitHub Actions）

当推送版本标签到 GitHub 时，GitHub Actions 会自动：
1. 构建固件
2. 创建 GitHub Release
3. 上传固件文件
4. 使用 CHANGELOG.md 生成发布说明

```bash
git tag -a "v2.1.0" -m "Release v2.1.0"
git push origin v2.1.0
```

## 更新 CHANGELOG.md

遵循 [Keep a Changelog](https://keepachangelog.com/) 格式：

```markdown
## [2.1.0] - 2024-01-XX

### Added
- 新功能

### Changed
- 变更的功能

### Fixed
- 修复的问题

### Removed
- 移除的功能
```

## 版本号更新位置

发布新版本时，需要更新以下位置：

1. ✅ `VERSION` 文件
2. ✅ `include/config.hpp` 中的版本宏
3. ✅ `CHANGELOG.md` 添加新版本条目
4. ✅ Git 标签 `v2.1.0`
5. ✅ GitHub Release（手动或自动）

## 最佳实践

1. **主版本号**: 重大架构变更、不兼容的 API 变更
2. **次版本号**: 新功能、新模块、向下兼容的改进
3. **修订号**: Bug 修复、性能优化、文档更新

4. **发布前检查**:
   - [ ] 所有测试通过
   - [ ] 代码已合并到 main 分支
   - [ ] CHANGELOG.md 已更新
   - [ ] 版本号已更新
   - [ ] 固件构建成功

5. **发布后**:
   - [ ] 验证 GitHub Release 已创建
   - [ ] 验证固件文件已上传
   - [ ] 通知用户新版本可用

