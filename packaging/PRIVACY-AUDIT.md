# WindowCue 用户数据处理专项审计

## 2026-10-13：1.0.2.0 功能变更补充

以下历史审计记录基于 2026-10-11 的源码快照。本次新增自启动后，其“无直接注册表写入”和“StartupTask 默认禁用”的结论不再描述当前版本。

当前 `Platform.cpp` 的 `QueryStartupStatus`、`SetStartupEnabled`、`InitializeStartupPreference` 使用包身份选择后端：MSIX 通过 WinRT `StartupTask`，便携版写入/删除自己的 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\WindowCue` 值并只读 `StartupApproved`。便携版新增 `startup-preference.txt`。MSIX 清单默认 `Enabled="true"`，首次激活后由 Windows 管理，用户/策略禁用状态优先。`WINDOWCUE_SETTINGS_DIR` 将启动注册改为 `HKCU\Software\WindowCue\StartupTests\<目录哈希>`，测试不会写真实登录项。

设置增加用户点击后用默认浏览器打开的固定 GitHub 项目 URL，不附带窗口标题、预览或配置。根目录和打包目录的隐私政策已同步上述行为及关闭/清除步骤。本补充是新增功能的源码说明，不是重做完整隐私审计，也不证明已安装 MSIX 的物理配置位置、注销/登录、卸载清理或 Store 认证已验证。

审计日期：**2026-10-11（Asia/Shanghai）**。方法：按实际入口、读取来源、内存转移、文件/系统接口终点逐项静态追踪，并独立交叉复核。政策产物：[PRIVACY.md](PRIVACY.md)。

## 1. 范围与证据等级

当前 `packaging/Build-Msix.ps1:117-125` 实际编译 `Main.cpp`、`Platform.cpp`、`SvgIconRenderer.cpp` 三个原生单元及资源，使用 `/MT` 和 Windows 库；不编译 WPF 或根目录聊天工具。CN/EN 的文本分支见 `Main.cpp:82-96`，数据处理逻辑共用。发布者名称来自 `packaging/partner-center.json:3-17`；联系邮箱来自用户本次明确提供的 `s1ksec@outlook.com`，不冒充源码发现。

读取范围覆盖上述三份 C++ 实现、头文件、资源、清单、原生构建/测试，旧 WPF 的全部应用源码、根目录聊天工具，以及打包/安装/签名/WACK 脚本。未审计 Windows、SDK、驱动或 .NET 框架内部实现；未对历史 EXE 做逆向或承诺它们与当前源码一致。

- **源码已确认**：具体可达实现及数据流；不等于本次动态复现。
- **未发现实现**：阅读当前运行单元、构建入口和依赖，并检索对应接口后未找到路径；不等于证明系统及所有二进制绝无该行为。
- **环境相关/未验证**：MSIX 物理路径、系统诊断、共享目录等，不能由应用源码确定。

本次未启动应用、执行自测、安装/卸载包、抓包或触发崩溃；未读取真实用户的配置内容或删除其数据。文档修改无须启动会自动重写配置的程序。工作目录没有 Git 元数据，使用下文哈希界定源文件快照；现有构建报告仅作辅助历史证据，不作为本次运行验证。

## 2. 原生版数据流与政策依据

下表中的路径均相对于项目根目录，行号为此次审阅的源文件行号。每一行均追踪到真实实现，负面结论限定于本次范围。

| 项目 / 政策对应内容 | 来源 → 处理 → 终点 | 内存、持久化、对外传输判定与依据 |
| --- | --- | --- |
| 窗口标题/类名 | `GetWindowTextW` / `GetClassNameW` → `EnumCallback` → `WindowInfo` / 窗口卡片 | 列表在内存；绑定标题另写盘。标题缓冲区最多 2048 个 UTF-16 代码单元，读取值去除首尾空白，不能保证标题完整。`Platform.cpp:22-36,118-146`；`WindowCueNative.h:28-43`；`Main.cpp:813-847,973-1124` |
| HWND / PID / 程序路径 | `GetWindowThreadProcessId`、`OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)` / `QueryFullProcessImageNameW` → 枚举、筛选、排序、匹配 | 列表在内存；HWND/路径进入绑定文件，PID 不在 JSON 字段中。`Platform.cpp:39-85,118-146,398-446,459-503` |
| 图标 | 窗口 `WM_GETICON`、窗口类图标，失败后 `SHGetFileInfoW(processPath)` → `CopyIcon` / 图标句柄 → UI | 本机 OS/目标应用交互；无图标文件写入。`Platform.cpp:88-108,449-456`。路径交给 Shell，不能排除网络路径或 Shell 扩展产生环境相关行为 |
| 实时缩略图 | 外部窗口 HWND → `DwmRegisterThumbnail` → `DwmQueryThumbnailSourceSize` / `DwmUpdateThumbnailProperties` | DWM 关系与显示状态，未发现窗口像素导出/图片写盘/上传。`Main.cpp:685-692,912-970`；`Platform.cpp:449-456`。`fSourceClientAreaOnly=FALSE`（`Main.cpp:960`），不能承诺排除边框或标题栏 |
| 图形位图与 SVG | 内嵌 sun/moon SVG → `SHCreateMemStream` / Direct2D 渲染 → 内存 HBITMAP | `SvgIconRenderer.cpp:65-139,151-158` 是应用主题图标，不能误报为抓取窗口缩略图。资源来源 `WindowCueNative.rc:4-7`；SVG 无外部资源链接 |
| 本地录入快捷键 | `WM_KEYDOWN` / `WM_SYSKEYDOWN` → `HandleKey` 的 `capturing` 检查 → `GetKeyState` → 一组 modifiers/key/text | 内存录入，不是全局输入文本记录。`Main.cpp:520-530,1174-1206,1443-1455`。失去焦点结束录入；无 `SetWindowsHookEx` 或 Raw Input 采集实现 |
| 已注册快捷键 | 保存/新录入组合 → `RegisterHotKey` → `WM_HOTKEY` → 显示/隐藏或 `ToggleWindow` | 组合交给本机 Windows；无触发次数/历史写盘。`Main.cpp:602-624,631-682,1461-1478`；`Platform.cpp:505-536` 的窗口激活及临时 `AttachThreadInput` 是本地焦点操作 |
| 绑定写盘 | 被选窗口的 path/title/class/HWND + modifiers/key/text/自身标记 → `ActiveBinding` → `SaveSettings` → `bindings.json` | 持久化全部八字段；标题是绑定时快照。`Main.cpp:609-625`；`WindowCueNative.h:45-61`；`Platform.cpp:547-579`。`WriteUtf8File:203-219` 使用 UTF-8、`CREATE_ALWAYS`，无应用加密、专门 ACL 或安全擦除 |
| 默认自身绑定 | 首次加载且无 self 绑定/初始化标记 → 自身 EXE 路径、WindowCue 标题/class/HWND + Alt+P → 保存及标记 | 不是所有写入都需要用户先点“绑定”。`Main.cpp:153-169,631-682`。不存在自身绑定的条件、已有组合冲突和已有标记影响初始化 |
| 设置位置 | 进程环境 `WINDOWCUE_SETTINGS_DIR` 优先，否则 `SHGetKnownFolderPath(FOLDERID_LocalAppData)` + `\WindowCue` → 文件 API | 默认逻辑路径 `%LOCALAPPDATA%\WindowCue`；覆盖字符串未限制本地/绝对路径。`Platform.cpp:150-168,171-219`。UNC、映射、同步或相对路径可改变实际终点；数据传给选定文件位置，不证明零网络流量 |
| 旧配置迁移 | `bindings.json` 优先，读取失败才尝试 `binding.json` → JSON parser → 恢复 | `Platform.cpp:327-363,538-579`；成功保存后尝试删除旧文件，非保存备份。无应用历史配置归档 |
| 主题 / 初始化标记 | 主题点击 → `SaveDarkModePreference`；首次 self 绑定/用户取消 self → `MarkSelfHotkeyInitialized` | `appearance.txt` 写 dark/light；`self-hotkey-initialized.txt` 写固定 initialized。`Main.cpp:561-568,625,682,1260-1268`；`Platform.cpp:582-610`。无时间戳或用户 ID，无自动到期 |
| 保留与解绑 | 取消绑定 → erase → Save；目标句柄失效 → erase → Save；加载失败匹配 → skip → Save | `Main.cpp:561-579,631-682,889-904,1480-1486,1650-1651`。500ms 检查周期不是保证删除时限；无 TTL。空列表尝试删除 current/legacy。`SaveBindings:457-459` 忽略保存结果，`Platform.cpp:553-556,578` 不检查删除结果，不能保证已清除 |
| 运行会话 / 关闭 | 列表刷新 → 替换标题/路径并管理图标和 DWM 关系；真正 `WM_DESTROY` → 释放 | `Main.cpp:813-887,1504-1514`；`Platform.cpp:449-456`。X/最小化/`WM_CLOSE` 走托盘隐藏（`Main.cpp:1145-1149,1492-1502`）；托盘“退出”才 `DestroyWindow`（`:1151-1166`）。隐藏保留列表、绑定和进程；不承诺内存安全擦除 |
| 位置 / DPI / 交互状态 | `GetWindowRect`、前台/可见/最小化检查、工作区/DPI、主窗口鼠标/滚动消息 → 筛选/布局/切换 | `Platform.cpp:57-85,129-133,505-536`；`Main.cpp:99-149,206-223,1281-1455,1628-1640`。未发现状态历史或鼠标轨迹文件写入 |
| 应用日志 | 全部原生运行单元和构建链接清单核查 | 未发现运行时 log 文件、`OutputDebugString`、事件日志或持续调试输出路径；不能据此否认 OS 日志。文件写入集中在 `Platform.cpp:203-219,547-610`；旧聊天工具日志单独见第 3 节 |
| 注册表 / 启动 | 原生业务代码无直接 Reg* / 注册表设置 API；MSIX 清单声明 `startupTask` | `AppxManifest.xml:24-39`，`Enabled=false`；`app.manifest:6-8` 为 asInvoker/uiAccess=false。启动任务由 Windows 管理，不能称整个安装过程不使用注册表 |
| 显式网络 / 遥测 / 崩溃 / SDK | 核查三个运行单元、完整入口、资源及构建链接；检索 HTTP/Internet/WinHTTP/socket/上传/Telemetry/广告/Sentry/Crashpad/Breakpad/WER/MiniDump/exception handler 等实现 | 未发现应用实现的请求、上传端点、遥测或自定义崩溃报告；无第三方分析/广告/崩溃 SDK。`CMakeLists.txt:8-31`；`build.ps1:9-13,83-95`；`Build-Msix.ps1:117-148`。只链接 Windows 库并静态链接 C/C++ 运行时，依赖检查拒绝非允许 DLL；不证明系统组件或其动态扩展没有外部行为 |
| 数据查看/删除能力 | UI 绑定显示 / 配置明文可读；取消绑定 / 托盘退出；手动删除四类设置文件 | 上述文件和退出/解绑实现支持政策操作。无专门隐私开关、配置安全擦除、源文档删除、备份删除或 OS 崩溃资料删除接口。再次启动可以重建默认 self 绑定 |

政策不宣称出售/不出售的业务承诺、法律依据、支持邮件期限、服务器地理位置、公司地址、未成年人定位或其他源码不能证明的运营事项。联系邮箱仅列为用户提供的联系渠道。

## 3. 同仓库旧代码：已审计，但不属于当前 Store 包

### WPF `WindowCue/`

- `Services/WindowEnumerator.cs:21-73,230-266` 枚举标题/class/HWND/PID/完整 EXE 路径；`Services/WindowIconProvider.cs:12-80` 从窗口或 Shell 取得图标并生成内存图像。
- `Services/DwmThumbnailManager.cs:23-31,70-76,132-167` 管理 DWM 缩略图，整个窗口源（`SourceClientAreaOnly=false`），未见图片保存。
- `Models/BindingSettings.cs:5-11` 的七字段不含原生版 `IsWindowCue`；`MainWindow.xaml.cs:434-450,472-474` 将绑定保存至 `Services/SettingsStore.cs:12-23,53-66` 的默认同名目录或环境覆盖目录，明文 JSON。两个分支的共享目录可能互相影响，不应在真实用户目录随意执行审计测试。
- `MainWindow.xaml.cs:104-171,396-405,462-474` 启动恢复/取消绑定/失效目标删除后保存；`SettingsStore.cs:53-87` 删除最后一个绑定及 legacy 文件，删除失败静默忽略。无固定保留期限；`MainWindow.xaml.cs:598-605,634-666` 区分关闭隐藏和真正退出。
- 快捷键设置是文本框 `PreviewKeyDown`（`MainWindow.xaml.cs:360-385`），全局触发用 `Services/HotkeyManager.cs:27-79` 的 RegisterHotKey/WM_HOTKEY；未发现通用键盘记录。
- `MainWindow.xaml.cs:621-624` 读取屏幕工作区和 DPI 缩放安排界面；`Services/WindowActivator.cs:41-72` 查询前台窗口/线程并临时连接输入队列完成本地窗口激活。这些状态在运行时处理，未发现写入配置或使用历史。
- 未发现应用实现的网络、日志、注册表操作、遥测或崩溃上传。`WindowCue.csproj:1-20` 仅有 .NET/WPF/Windows Forms 框架和图标资源，无 `PackageReference`；不代表 .NET/Windows 内部行为已审计。

### 根目录 `CodexDetachedChatHotkey.cs`

- `375-403` 枚举 ChatGPT 窗口并将**未脱敏的窗口标题、HWND、类名、尺寸、DPI、匹配分数**写入日志；标题读取使用 512 字符缓冲区（`689-693`），可能截断。`453-472` 读取目标进程名。没有缩略图实现。
- `273-305` 的 UI Automation 遍历所有后代元素并读取 `Current.Name`，再匹配“在新窗口中打开”名称。因此不能沿用根目录 README 的“不读取聊天内容”作为严格隐私事实：可访问范围包括目标应用通过自动化树暴露的名称，是否含具体聊天文本取决于目标应用。
- `203-209` 固定 Alt+X，没有快捷键 JSON；`671-686` 发送 Ctrl+Alt+N，`302-305` 调用目标应用 InvokePattern。向 ChatGPT 发送的是本机命令，ChatGPT 后续联网行为不由此源码证明。
- `787-799` 在 **`%LOCALAPPDATA%\CodexDetachedChatHotkey\hotkey.log`** 追加本地时间和消息；`107-111` 记录完整异常。**无轮换、期限或自动删除。** 用户需退出该工具后手动删除该文件/专属目录。当前 WindowCue 原生政策中的“不存在应用日志实现”不能覆盖这个工具。
- 未发现该辅助工具实现的网络 API、注册表、遥测或崩溃上传；普通异常写本地日志不是远程崩溃报告。

## 4. 构建、测试与系统边界

- 原生自测入口保留在 Release EXE：`Main.cpp:1537-1597` 的 `--binding-self-test` 枚举窗口并只保存自身构造的测试窗口绑定，仍使用正式设置函数。**独立手工执行且未指定隔离目录，可能重写真实配置。** `Build-Msix.ps1:154-164` 为自测指定 `artifacts/<run>/test-settings`；`--svg-self-test:1599-1608` 渲染内嵌资源。`tests/UiRegression.cpp:1-10,30-82` 是独立测试程序，要求覆盖目录，写合成测试绑定/标记，不是正常业务日志或截图保存。
- `Tools.ps1:60-68` **读取** HKLM Windows Kits 安装路径，目的为寻找开发工具，非应用用户信息采集。`Tools.ps1:174-205` 将工具 stdout/stderr 写至显式日志路径；`Build-Msix.ps1:110-152,181-221` 保存编译/打包/依赖/路径/哈希报告。它们在 `packaging/artifacts`、`packaging/output`，无自动保留期限；由开发者手动管理，不作为应用运行用户日志打入包。
- `Sign-Local.ps1:25-48,53-67` 使用或创建 `CurrentUser\My` 的不可导出测试私钥，导出公开 `.cer`，保存签名日志/报告；`Install-Local.ps1:17-30` 仅在显式 `-TrustCertificate` 时向 `LocalMachine\TrustedPeople` 导入信任证书并本地安装。证书/安装状态是开发测试系统变更，不是 WindowCue 设置收集；不因卸载应用就承诺证书自动删除。
- `Test-Wack.ps1:14-22` 调用外部微软认证工具并保存报告，不能由包装脚本确认该工具内部所有行为。现有打包脚本未发现主动下载/上传请求；验证证书、Windows 工具等系统行为不等同于已做网络验证。
- 仓库 `dotnet-install.ps1:408-467,1112,1225-1226` 明确有开发依赖下载请求，指向微软下载源/重定向或调用者指定源；不属于原生应用或 MSIX 包。WPF `build.ps1:8-10,24-33` 设置 CLI 遥测退出变量并调用 publish，这不能用于证明原生应用无遥测，也不能证明所有 .NET 开发工具行为。
- 辅助历史报告 `output/s1ksec.WindowCue_1.0.0.0_x64_CN_Store_20261011-224508-27a3834b.msix.report.json` 记录 11 个 Windows 系统 DLL、无外部分发运行库；本次未重新打包或复核 EXE 与该源码快照的一致性。XML/SVG 中的 `http://...` 是命名空间标识，不是请求实现。

## 5. 无法确认的行为与待验证方法

| 无法确认的事项 | 原因与验证范围 |
| --- | --- |
| MSIX 的具体物理配置路径、旧便携配置迁移、升级保留和卸载残留 | 源码使用逻辑 LocalAppData，清单无应用自有迁移/卸载删除器；不同系统和已有文件可能改变文件重定向。需在隔离 Windows 用户中安装目标包，以文件操作跟踪和更新/卸载前后对照确认，不承诺统一 LocalState/LocalCache 路径或卸载清空 |
| Windows / Store 诊断、WER 崩溃报告内容、保留期限及开发者后台可见信息 | 无应用自定义实现不能否定 OS 功能；需结合系统诊断/错误报告配置、实际平台权限和运行证据。不得编造未使用 SDK，也不得声明没有任何崩溃报告 |
| 系统图标提取、Shell 扩展、图形驱动及共享/同步目录的网络行为 | 相关系统实现不在此源码，覆盖目录不校验本地路径；需隔离路径实验、已配置扩展清单和网络/文件追踪。远程目录接收方由环境配置决定，不能声称所有数据永不离开设备 |
| 磁盘/内存残留、分页、备份及同步副本 | 应用只有普通文件删除和资源释放，没有安全擦除或备份控制；不声明可恢复数据已不可逆销毁 |
| 用户主动发邮件后的支持数据处理 | 邮箱已由用户提供，但邮箱服务、人工支持、保留安排和共享对象不在应用源码；政策只提供联系方法，不虚构保留期限或自动上传行为 |
| 已发布或历史二进制及后续版本 | 源码审阅不等于产物逆向、运行或供应链证明；发布前用对应源快照和包内容核查，新增功能需要更新政策 |

## 6. 隐私设计观察与提交事项

以下为已确认设计事实及复核建议，**不是已复现安全漏洞**。未开展独立漏洞利用验证，不将隐私观察计入漏洞实例数；共 2 个独立隐私设计事项。

### A-01 明文绑定元数据（Info / 源码已确认）

1. **位置：** `Main.cpp:613-624`、`Platform.cpp:203-219,565-579`。
2. **类型/根因：** 读取到的窗口标题和程序完整路径直接写明文 JSON，未增加加密或专门文件访问权限。
3. **数据链：** 窗口标题/程序路径 → BindingSettings → EscapeJson → UTF-8 文件写入。JSON 转义处理格式，不是隐私脱敏。
4. **前提：** 有读取配置文件的本机权限，或配置目录被共享、同步、备份；不是已证明的远程越权。
5. **影响：** 绑定时标题可持续暴露文档/网页名称；路径可含用户名。未绑定窗口列表不整体写盘。
6. **等级/证据：** Info；源码已确认，未验证攻击场景。政策明确明文和敏感内容，不能写“加密存储”。
7. **建议：** P1 在提交前评估持久化标题的必要性、数据最小化及合适的加密/访问保护；如修改实现，另行验证并更新政策。本次只交付审计和文档，不修改业务代码。
8. **复核：** 使用合成敏感标题和用户目录路径，核查所有保存/恢复/迁移入口、失败处理及删除后残留；不得在真实配置上运行自测。

### A-02 敏感标题/预览没有分类、遮盖或独立隐私开关（Info / 源码已确认）

1. **位置：** `Platform.cpp:57-146`、`Main.cpp:609-625,685-692,924-970,1648-1649`。
2. **类型/根因：** 筛选关注窗口可用性，不识别内容敏感性；启动即枚举/预览，绑定持久化不含敏感内容告知或分类控制。
3. **数据链：** 任意符合条件窗口 → 标题列表/DWM 全窗口预览；选中并绑定 → 标题 JSON。
4. **前提：** 其他应用在标题/可视内容中展示个人或敏感数据；屏幕被看见或共享，或用户保存绑定。
5. **影响：** 第三方应用中的内容可被显示或部分持久化；取消绑定不关闭该窗口的预览。
6. **等级/证据：** Info；源码已确认设计边界，不宣称已经违反法规或已被商店拒绝。
7. **建议：** P1 评估真实使用中的敏感数据、适用同意机制和隐私控制；公开可访问政策是提交动作，不能写成应用已实现同意或过滤。
8. **复核：** 使用合成窗口验证启动/未绑定/隐藏/退出、解绑后预览及设置写盘，审核首次告知和用户操作是否符合产品预期。

P0：本次未确认需要紧急遏制的对外传输或远程接收路径。P1：公开政策 URL、复核上述明文与敏感内容设计及已安装 MSIX 删除行为。P2：核实系统诊断/支持运营信息、每次版本更新重新审查数据流；文件写入/删除失败应有可理解的反馈。

Microsoft Store [政策 7.19 §10.5](https://learn.microsoft.com/en-us/windows/apps/publish/store-policy-archive/store-policy-7-19#105-personal-information) 要求 Win32 应用提供隐私政策 URL并说明访问、用途、存储、安全和控制；§10.5.4/10.5.5 涉及安全保护与高度敏感信息的同意要求。因此，准确文档不等于当前明文设计和界面已通过认证。审阅日官方还公布了 [7.20](https://learn.microsoft.com/en-us/windows/apps/publish/store-policies#105-personal-information)，页面载明 2026-10-22 生效；实际提交时复核适用版本。

提交时将 `PRIVACY.md` 正文发布到实际公开可访问的网址，并填写 Partner Center 隐私政策 URL；本次未发布网页、未编造网址、未上传 Store。若同时公开审计记录，应保持其相对文档链接可用。不能将本地文件路径当作提交 URL。

系统参考：[DWM 缩略图机制](https://learn.microsoft.com/en-us/windows/win32/dwm/thumbnail-ovw)、[包装桌面应用的文件系统行为](https://learn.microsoft.com/en-us/windows/msix/desktop/desktop-to-uwp-behind-the-scenes)、[Windows Error Reporting](https://learn.microsoft.com/en-us/windows/win32/wer/windows-error-reporting)。这些说明用于限定系统边界，不作为本机行为已验证的证据。

## 7. 审阅源文件 SHA-256 快照

| 源文件 | SHA-256 |
| --- | --- |
| `WindowCue.Native/src/Main.cpp` | `AA513F78F1FDEA4BEEAE10C38934E08B0A8E0DBEC90D423B797277924D55F3E6` |
| `WindowCue.Native/src/Platform.cpp` | `01CAEFFE78238B81653CFFC4EB410EB0B97B4515852FFBAD64118EC899321EDE` |
| `WindowCue.Native/src/SvgIconRenderer.cpp` | `299E26FB637A63973E3AD838EC462E97686841F2AAA30491A532C0DCBB762ABB` |
| `WindowCue.Native/src/WindowCueNative.h` | `444E180D34D39CD39217FA8471316FCCBBD3136D4760B6DC2B5017B38789C70C` |
| `packaging/Build-Msix.ps1` | `09618BEA04950FBD1373C2F3C4838B2EDABF3588F77A0B5071E1D31A6E55B6D2` |
| `packaging/AppxManifest.xml` | `1CF825ECA5BA5053E6CBE16F8DBDDE84CC0622283F3591D7E0BF98D5ABA53541` |
| `WindowCue/MainWindow.xaml.cs` | `3110D357E27554CA40E719A6E3AB02400ED9257938FE2A4323DA2870D60DD2F6` |
| `WindowCue/Services/SettingsStore.cs` | `499F51D4F13166CE31224A6CA508AAF6B851892541FAF70EF945DDA39841B261` |
| `CodexDetachedChatHotkey.cs` | `599F5404FD4BC599A9AEC0D0B116B2731F40BCB53ABBBBA35E2C27DCABD023D6` |
