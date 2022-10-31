#include "conclusionpage.hpp"

#include <QDebug>

#include "mainwizard.hpp"

Wizard::ConclusionPage::ConclusionPage(QWidget* parent)
    : QWizardPage(parent)
{
    mWizard = qobject_cast<MainWizard*>(parent);

    setupUi(this);
    setPixmap(QWizard::WatermarkPixmap, QPixmap(QLatin1String(":/images/intropage-background.png")));
}

void Wizard::ConclusionPage::initializePage()
{
    // Write the path to openmw.cfg
    if (field(QLatin1String("installation.retailDisc")).toBool() == true)
    {
        QString path(field(QLatin1String("installation.path")).toString());
        mWizard->addInstallation(path);
    }

    if (!mWizard->mError)
    {
        if ((field(QLatin1String("installation.retailDisc")).toBool() == true)
            || (field(QLatin1String("installation.import-settings")).toBool() == true))
        {
            qDebug() << "IMPORT SETTINGS";
            mWizard->runSettingsImporter();
        }
    }

    if (!mWizard->mError)
    {
        if (field(QLatin1String("installation.retailDisc")).toBool() == true)
        {
            textLabel->setText(
                tr("<html><head/><body><p>OpenMW 向导成功在你的电脑安装了晨风。</p> \
                                  <p>点击完成(Finish)关闭向导。</p></body></html>"));
        }
        else
        {
            textLabel->setText(tr(
                "<html><head/><body><p>OpenMW 向导成功修改了已经安装的晨风。</p> \
                                  <p>点击完成(Finish)关闭向导。</p></body></html>"));
        }
    }
    else
    {
        textLabel->setText(
            tr("<html><head/><body><p>OpenMW 向导无法在你的电脑安装晨风。</p> \
                              <p>请把你遇到的任何 bugs 报告给我们的 \
                              <a href=\"https://gitlab.com/OpenMW/openmw/issues\">bug 跟踪器</a>.<br/>确保提供安装日志。</p><br/></body></html>"));
    }
}

int Wizard::ConclusionPage::nextId() const
{
    return -1;
}
