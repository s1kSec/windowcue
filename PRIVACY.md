# WindowCue 隐私政策 / Privacy Policy

最后更新 / Last updated: **2026-10-13**

## 中文

### 1. 适用范围与联系方式

本政策适用于发布者 **s1ksec** 的 WindowCue 原生 Windows 桌面应用（`WindowCue.Native`），包括由当前 `packaging/Build-Msix.ps1` 构建的 MSIX 版本，供 Microsoft Store 提交使用。中文和英文界面使用相同的数据处理实现。本政策不涵盖仓库中的旧 WPF 应用 `WindowCue/` 或独立工具 `CodexDetachedChatHotkey`。

隐私问题请联系：[s1ksec@outlook.com](mailto:s1ksec@outlook.com)。此邮箱由发布者提供；应用内没有发送支持邮件、自动附加配置或上传诊断资料的功能。

WindowCue 会访问本机窗口信息，并保存部分绑定设置。因此，“没有向开发者上传数据”不等于“不访问或不保存用户数据”。以下说明依据原生源码，2026-10-13 补充自启动设置及 GitHub 入口的数据处理；原始审计证据和后续补充边界见 [隐私专项审计记录](PRIVACY-AUDIT.md)。

### 2. 访问的数据及用途

| 数据 | 何时访问及用途 | 处理方式 |
| --- | --- | --- |
| 窗口标题、窗口类名、窗口句柄（HWND）、进程 ID、程序完整路径及图标 | 启动及刷新窗口列表时，枚举符合筛选条件的顶层窗口，用于显示、排序、识别绑定目标及窗口切换；无需先绑定该窗口 | 窗口列表主要在内存中处理；绑定窗口的部分信息还会保存，见下一节 |
| 窗口外观及内容的实时缩略图、源窗口尺寸 | 通过 Windows 桌面窗口管理器（DWM），在 WindowCue 窗口内显示窗口预览 | 本机 DWM 实时显示；应用保存缩略图句柄和显示状态，当前实现不把预览保存为图片文件，也没有上传预览的实现 |
| 快捷键组合 | 在 WindowCue 的快捷键录入状态下接收按键及修饰键，用于配置快捷键；使用 Windows 注册快捷键机制接收已绑定组合的触发通知 | 未确认绑定的组合在内存中处理；确认后的组合保存到配置文件。当前实现没有通用键盘钩子或连续记录其他应用输入文本的功能 |
| 明暗主题偏好、默认自身快捷键初始化状态 | 记住外观，以及自身快捷键是否已初始化，避免在取消后再次自动创建 | 保存到本机设置文件 |
| 自启动偏好及 Windows 启动状态 | 记住当前用户登录时是否启动，并显示应用或 Windows 的禁用状态 | 便携版保存偏好文件、读写自己的当前用户 Run 值并只读 StartupApproved；MSIX 使用 Windows StartupTask API |
| 窗口位置、尺寸、可见/最小化状态，前台窗口、屏幕工作区及 DPI，WindowCue 内的选择、滚动和鼠标状态 | 筛选窗口、安排界面和预览、处理交互，以及显示、恢复或最小化目标窗口 | 运行时内存处理，当前实现未将这些交互或状态写成使用历史 |

窗口标题、程序路径和预览内容可能包含姓名、Windows 用户名、文档名称、网页标题、聊天内容或其他个人及敏感信息。预览可能显示源窗口的完整外观，包括非客户区；应用没有自动识别、过滤或遮盖敏感内容的实现。即使未绑定某个窗口，该窗口仍可能出现在列表和预览中。

### 3. 持久化数据与存储位置

应用通过 Windows 的当前用户本地应用数据目录定位设置，默认逻辑位置为 **`%LOCALAPPDATA%\WindowCue\`**。

| 文件 | 保存内容及目的 |
| --- | --- |
| `bindings.json` | 每个绑定的 `ProcessPath`（程序完整路径）、`WindowTitle`（绑定时的窗口标题）、`ClassName`、`WindowHandle`、`IsWindowCue`、`Modifiers`、`VirtualKey` 和 `HotkeyText`；用于恢复、识别和注册窗口快捷键。窗口标题之后变化时，绑定文件中的标题可能仍是绑定时的值 |
| `binding.json` | 兼容读取的旧单绑定配置；当前实现写入 `bindings.json`，成功写入后尝试删除旧文件 |
| `appearance.txt` | `dark` 或 `light`，用于记住主题 |
| `self-hotkey-initialized.txt` | 固定的 `initialized` 标记，用于记住自身快捷键已初始化或被取消 |
| `startup-preference.txt` | 便携版的 `enabled` 或 `disabled` 自启动偏好，避免关闭后自动重新注册 |

首次启动时，应用可能自动创建并保存自身默认 **Alt+P** 绑定，包括 WindowCue 自身的程序路径、类名和句柄；这不要求用户先手动绑定其他窗口。已有自身绑定或初始化标记会影响是否创建默认绑定。

这些配置采用**明文 UTF-8** 保存，应用没有为其增加加密或独立密码保护，也没有设置专门的文件访问权限。访问保护取决于 Windows 账户、所在目录的权限和设备的保护设置；拥有文件读取权限的其他程序或人员可以读取这些文件。

启动环境变量 `WINDOWCUE_SETTINGS_DIR` 可以覆盖默认目录。若你的启动环境、Windows 目录设置或所选目录使用网络共享、映射盘或同步服务，配置可能存放或传输到该位置。应用没有把配置目录强制限制为本地磁盘，也没有内置云同步服务。

MSIX 安装后的物理存储位置可能受 Windows 包装应用的文件重定向及既有文件影响。默认逻辑目录不代表所有安装环境下的唯一物理位置；当前审计未验证已安装 MSIX 的实际路径和卸载清理结果。

### 4. 保留期限

- 窗口列表、图标和缩略图关系供当前运行会话使用；列表更新时替换或释放相应资源，真正退出时释放窗口列表、图标及缩略图关系。隐藏到托盘或最小化不等于退出，内存状态和快捷键处理仍会保留。
- 绑定配置没有按天数设定的到期时间。取消绑定会更新保存的绑定列表；应用运行中检测到目标窗口句柄失效，或启动时无法恢复目标时，会移除相应绑定并更新配置。最后一个绑定移除时，应用尝试删除当前及旧绑定文件。
- 主题偏好、自启动偏好和初始化标记没有自动到期或定期删除机制，保留至用户手动删除。
- 保存和删除依赖文件系统操作成功。当前实现不会向用户可靠报告所有写入或删除失败，因此需要彻底清除数据时，应退出应用后检查并手动删除文件。

以上是应用自身的保留逻辑，不承诺清除 Windows 分页文件、系统诊断资料、备份、同步副本或磁盘上的可恢复痕迹。

### 5. 共享、网络、日志、遥测和崩溃报告

WindowCue 通过本机 Windows API 与系统交互，传递窗口句柄、快捷键组合及相关参数，查询图标并请求 DWM 渲染预览，以完成上述功能。显示的标题和预览也可能被看到屏幕的人员或你使用的屏幕共享软件看到。

在当前原生应用源码及构建依赖中，未发现应用实现的 HTTP/套接字请求、数据上传接口、开发者远程接收端、广告或分析遥测 SDK、第三方崩溃报告 SDK、应用日志写入或自定义崩溃转储/上传功能。当前实现没有把窗口标题、预览或配置自动发送给开发者、广告商或分析服务的路径。窗口信息和绑定设置使用上述文件；自启动按下一段使用 Windows 任务 API 或当前用户注册表。

这些结论不涵盖 Windows、Microsoft Store、系统组件或驱动自身的行为。Windows 错误报告、Store 安装/更新及平台诊断可能依照系统设置和 Microsoft 的规则处理数据；源码不能确认它们在你的设备上是否启用、实际提交哪些数据、保留多久，或发布者是否通过平台取得报告。请参阅 [Microsoft 隐私声明](https://www.microsoft.com/en-us/privacy/privacystatement)及你的 Windows 诊断和错误报告设置。

Windows 对网络程序路径的图标查询，以及你指定的网络或同步目录，也可能产生系统或其他软件的网络行为。没有内置上传功能不代表所有运行环境都绝无网络活动。

商店包声明由 Windows 管理的 `WindowCueStartup`，首次激活后默认启用。应用通过 StartupTask API 查询或开关，尊重 Windows 用户或策略禁用状态。便携版首次正常运行默认在 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` 写入自己的 `WindowCue` 值，内容为带引号的 EXE 路径和 `--startup`，关闭开关会删除该值；应用只读取 Windows `StartupApproved\Run\WindowCue` 状态，不修改系统的禁用选择。登录启动后静默驻留托盘，执行与手动启动相同的数据处理。设置中的 GitHub 图标仅在点击后通过默认浏览器打开 `https://github.com/s1kSec/windowcue`；浏览器、GitHub 及网络服务可能按各自规则处理访问元数据，应用不会附带窗口信息或配置。

### 6. 查看、控制与删除

1. **查看数据：** 在 WindowCue 中查看窗口列表及已绑定快捷键；使用文本编辑器查看上述配置文件。MSIX 或自定义目录请先确认实际存储位置。
2. **删除单个绑定：** 选择已绑定窗口，点击“取消绑定”。这会更新绑定配置；只要窗口仍符合列表条件，其标题和预览仍可能被显示。
3. **停止运行时处理：** 右键系统托盘中的 WindowCue 图标并选择“退出”。点击主窗口关闭按钮或最小化只会隐藏到托盘。
4. **删除全部应用设置：** 先在齿轮设置中关闭自启动，再退出正在使用同一设置目录的所有 WindowCue 实例，删除该目录内的 `bindings.json`、旧 `binding.json`、`appearance.txt`、`self-hotkey-initialized.txt` 和 `startup-preference.txt`，并确认文件已不存在。若使用 MSIX 重定向、自定义位置、备份或同步副本，还需检查相关位置。删除初始化标记后，下次启动可能再次生成默认 Alt+P 绑定及相关文件；删除便携版自启动偏好后，下一次正常启动可能重新启用自启动。
5. **卸载及开机启动：** 齿轮设置可开关自启动；Windows 用户禁用时通过系统“启动应用”重新启用，策略禁用状态不能由应用覆盖。删除便携程序前先关闭自启动，以移除其当前用户 Run 值；仅删除 EXE 不会自动清除该值。可在 Windows 设置中卸载 MSIX，并在“启动应用”中关闭任务。卸载是否清除所有配置取决于实际存储及 Windows 行为；便携版、既有非重定向文件或自定义目录的配置可能保留，应按上一步检查。

应用目前没有单独关闭窗口枚举或预览的隐私开关，没有安全擦除、备份清除或系统崩溃报告删除功能。其配置删除也不会删除源应用中的文档、网页或聊天内容。

### 7. 无法从当前源码确认的事项

以下事项单独列示，不作为已验证的隐私承诺：安装后的 MSIX 物理路径及更新/卸载残留；Windows、Store、Shell 扩展、驱动和错误报告的具体数据处理及保留期限；自定义共享/同步目录的接收方、权限和副本；操作系统分页、备份及数据恢复痕迹；发布者在 Store 后台能取得的诊断资料，以及你主动发送支持邮件后邮箱服务和人工支持的处理、共享及保留安排。后续版本或非当前源码构建的二进制需要另行核查。

## English

### 1. Scope and contact

This policy applies to the native Windows desktop version of **WindowCue**, published by **s1ksec** (`WindowCue.Native`), including MSIX versions built by the current `packaging/Build-Msix.ps1`, and is intended for Microsoft Store submission. The Chinese and English interfaces use the same data-handling implementation. It does not cover the older WPF application in `WindowCue/` or the separate `CodexDetachedChatHotkey` utility.

For privacy questions, contact [s1ksec@outlook.com](mailto:s1ksec@outlook.com). The publisher supplied this address. The application has no feature that sends support email, automatically attaches configuration files, or uploads diagnostic material.

WindowCue accesses information about windows on your device and saves some binding settings. Absence of uploads to the developer does not mean absence of access to or storage of user data. This policy reflects native source, with startup settings and the GitHub entry added on 2026-10-13. Original audit evidence and the later supplement's limits are documented in the [privacy audit record](PRIVACY-AUDIT.md).

### 2. Data accessed and purposes

| Data | When accessed and purpose | Handling |
| --- | --- | --- |
| Window titles, class names, window handles (HWND), process IDs, full executable paths, and icons | Eligible top-level windows are enumerated at startup and during list refreshes for display, sorting, target identification, and window switching. A window need not be bound first | The list is mainly handled in memory; some information about bound windows is also saved as described below |
| Live previews of window appearance and content, and source-window dimensions | Windows Desktop Window Manager (DWM) displays previews inside WindowCue | Local DWM rendering; the application holds thumbnail handles and display state. The current implementation does not save previews as image files or implement preview uploads |
| Keyboard shortcut combinations | Key and modifier events received while recording a shortcut in WindowCue configure a binding. Windows registered-hotkey notifications trigger saved shortcuts | Unconfirmed combinations are handled in memory; confirmed combinations are saved. The current implementation has no general keyboard hook or continuous recording of text typed in other applications |
| Light/dark preference and initialization state of WindowCue's own default shortcut | Remember appearance and whether the app's shortcut was initialized, including after cancellation | Saved in settings files |
| Startup preference and Windows startup state | Remember whether to start at current-user sign-in and show app or Windows disable states | The portable app saves a preference, reads/writes its own current-user Run value and only reads StartupApproved; MSIX uses Windows StartupTask APIs |
| Window bounds, visibility/minimized state, foreground window, desktop work area and DPI, and selection, scrolling and pointer state within WindowCue | Window filtering, layout, previews, interaction, and showing, restoring or minimizing target windows | Runtime memory handling; the current implementation does not save these interactions or states as a usage history |

Titles, executable paths and previews may contain names, Windows usernames, document names, webpage titles, chat content, or other personal or sensitive information. Previews may show the whole source window, including non-client areas. The application does not automatically identify, filter or redact sensitive content. Unbound windows may also appear in the list and previews.

### 3. Persistent data and storage

The application obtains the current user's local application-data folder from Windows. Its default logical settings location is **`%LOCALAPPDATA%\WindowCue\`**.

| File | Contents and purpose |
| --- | --- |
| `bindings.json` | Each binding's `ProcessPath` (full executable path), `WindowTitle` (title at binding time), `ClassName`, `WindowHandle`, `IsWindowCue`, `Modifiers`, `VirtualKey`, and `HotkeyText`, used to restore, identify and register window shortcuts. A saved title may remain the title at binding time after the displayed title changes |
| `binding.json` | A legacy single-binding configuration that can be read for compatibility. The current implementation writes `bindings.json` and attempts to delete the legacy file after a successful write |
| `appearance.txt` | `dark` or `light`, to remember the theme |
| `self-hotkey-initialized.txt` | A fixed `initialized` marker, to remember initialization or cancellation of the app's own shortcut |
| `startup-preference.txt` | The portable app's `enabled` or `disabled` startup preference, to avoid re-registering after startup has been turned off |

On first launch, the application may automatically create and save its own default **Alt+P** binding, including its executable path, window class and handle. This does not require first manually binding another window. Existing self-bindings or an initialization marker affect whether the default is created.

Settings are saved as **plain-text UTF-8**. The application adds no encryption, separate password protection or custom file permissions. Protection depends on your Windows account, directory permissions and device settings. Other programs or people with file-read permission can read the files.

The launch environment variable `WINDOWCUE_SETTINGS_DIR` can override the default directory. If your launch environment, Windows folder configuration or chosen location uses a network share, mapped drive or synchronization service, settings may be stored in or transmitted to that location. The application does not require the directory to be on a local disk and has no built-in cloud synchronization service.

For MSIX installations, physical storage can depend on Windows file redirection and existing files. The logical default is not a verified universal physical path. The installed MSIX path and uninstall cleanup have not been verified in this audit.

### 4. Retention

- Window lists, icons and thumbnail relationships serve the current running session. Resources are replaced or released during list updates; the window list, icons and thumbnail relationships are released on actual exit. Hiding to the tray or minimizing keeps the process, memory state and hotkey handling active.
- Binding settings have no expiry expressed in days. Unbinding updates the saved list. When a target handle becomes invalid during operation, or a target cannot be restored at startup, the corresponding binding is removed and settings are updated. Removing the last binding attempts to delete both current and legacy binding files.
- Theme preferences, startup preferences and the initialization marker have no automatic expiry or periodic deletion; they remain until manually deleted.
- Saving and deletion depend on successful file operations. The current implementation does not reliably report all write or deletion failures. For complete removal, exit the application and inspect and manually delete the files.

This describes the application's retention logic. It does not promise removal of Windows paging files, system diagnostics, backups, synchronized copies or recoverable disk traces.

### 5. Sharing, network activity, logs, telemetry and crash reports

WindowCue interacts with local Windows APIs, passing window handles, shortcut combinations and related parameters, requesting icons and asking DWM to render previews for these functions. People who can see your screen, or screen-sharing software you use, may also see displayed titles and previews.

No application-implemented HTTP/socket requests, data-upload interface, developer remote endpoint, advertising or analytics SDK, third-party crash-reporting SDK, application log writer, or custom crash-dump/upload feature was found in the current native source and build dependencies. The current implementation has no path that automatically sends titles, previews or settings to the developer, advertisers or analytics services. Window information and bindings use the files described above; startup uses Windows task APIs or the current-user registry as described below.

These conclusions do not cover Windows, Microsoft Store, system components or drivers themselves. Windows Error Reporting, Store installation/updates and platform diagnostics may process data under system settings and Microsoft's rules. The source cannot establish whether those services are enabled on your device, what they submit, how long they retain data, or whether the publisher obtains platform reports. See the [Microsoft Privacy Statement](https://www.microsoft.com/en-us/privacy/privacystatement) and your Windows diagnostic and error-reporting settings.

Windows icon lookup for network executable paths, and network or synchronized settings locations you configure, may also involve network activity by the operating system or other software. Absence of a built-in uploader does not guarantee that every environment has no network activity.

The Store package declares the Windows-managed `WindowCueStartup`, enabled by default after first activation. The app queries or toggles it using StartupTask APIs and respects user and policy disable states. On its first normal launch, the portable app creates its own `WindowCue` value under `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, containing the quoted EXE path and `--startup`; turning startup off deletes this value. The app only reads Windows `StartupApproved\Run\WindowCue` and does not change Windows' disable choice. Sign-in startup runs quietly in the tray with the same data handling as a manual launch. Only when clicked, the GitHub icon opens `https://github.com/s1kSec/windowcue` in the default browser. The browser, GitHub and network services may process visit metadata under their own rules; the app does not attach window information or settings.

### 6. Access, controls and deletion

1. **Access:** Inspect the window list and bindings in WindowCue, and open the settings files in a text editor. First identify the actual storage location for MSIX or custom directories.
2. **Remove a binding:** Select a bound window and click **Unbind**. This updates saved bindings. The window's title and preview may still be displayed while the window qualifies for the list.
3. **Stop runtime processing:** Right-click the WindowCue system-tray icon and select **Exit**. Closing or minimizing the main window only hides it to the tray.
4. **Remove all settings:** First turn startup off in gear settings, exit every WindowCue instance using the same directory, then delete `bindings.json`, legacy `binding.json`, `appearance.txt`, `self-hotkey-initialized.txt` and `startup-preference.txt` there and verify their removal. Also check MSIX redirected locations, custom locations, backups or synchronized copies where applicable. Removing the initialization marker may recreate the default Alt+P binding and related files. Removing the portable startup preference may re-enable startup on the next normal launch.
5. **Uninstall and startup:** Use gear settings to turn startup off or on. A Windows user disable state must be re-enabled in **Startup apps**; the app cannot override policy disable states. Turn startup off before deleting the portable EXE to remove its current-user Run value; deleting the EXE alone does not remove it. Uninstall MSIX through Windows Settings and disable its startup task there. Complete settings removal depends on actual storage and Windows behavior. Portable-version files, existing unredirected files or custom-directory settings may remain; inspect them as described above.

The application currently has no separate privacy switch for window enumeration or previews, and no secure-erasure, backup-removal or system-crash-report deletion feature. Deleting settings does not delete documents, webpages or chat content from source applications.

### 7. Matters not established by the current source

These are separate uncertainties, not verified privacy assurances: installed MSIX physical paths and update/uninstall remnants; specific handling and retention by Windows, Store, Shell extensions, drivers and error reporting; recipients, permissions and copies for custom shared/synchronized storage; operating-system paging, backups and recovery traces; diagnostic information available to the publisher in Store dashboards; and mailbox-service or manual-support handling, sharing and retention after you choose to send support email. Future versions and binaries not built from the reviewed source require a separate review.
