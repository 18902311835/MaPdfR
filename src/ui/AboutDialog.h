#pragma once

#include <QDialog>

// 「帮助 → 关于 MaPdfR」窗口：软件信息、第三方组件与许可。
// 版式参考同级 VSDX 项目的 AboutDialog；内容按本工程的实际依赖填写。
class AboutDialog : public QDialog {
    Q_OBJECT

public:
    enum Page {
        AboutPage = 0,
        ThirdPartyPage,
        LicensePage
    };

    explicit AboutDialog(QWidget* parent = nullptr,
                         Page initialPage = AboutPage);
};
