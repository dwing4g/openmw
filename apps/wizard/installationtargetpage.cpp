#include "installationtargetpage.hpp"

#include <string>

#include <QDebug>
#include <QFileDialog>
#include <QMessageBox>

#include <components/files/configurationmanager.hpp>
#include <components/files/conversion.hpp>

#include "mainwizard.hpp"

Wizard::InstallationTargetPage::InstallationTargetPage(QWidget* parent, const Files::ConfigurationManager& cfg)
    : QWizardPage(parent)
    , mCfgMgr(cfg)
{
    mWizard = qobject_cast<MainWizard*>(parent);

    setupUi(this);

    registerField(QLatin1String("installation.path*"), targetLineEdit);
}

void Wizard::InstallationTargetPage::initializePage()
{
    QString path(QFile::decodeName(Files::pathToUnicodeString(mCfgMgr.getUserDataPath()).c_str()));
    path.append(QDir::separator() + QLatin1String("basedata"));

    QDir dir(path);
    targetLineEdit->setText(QDir::toNativeSeparators(dir.absolutePath()));
}

bool Wizard::InstallationTargetPage::validatePage()
{
    QString path(field(QLatin1String("installation.path")).toString());

    qDebug() << "Validating path: " << path;

    if (!QFile::exists(path))
    {
        QDir dir;

        if (!dir.mkpath(path))
        {
            QMessageBox msgBox;
            msgBox.setWindowTitle(tr("创建目标出错"));
            msgBox.setIcon(QMessageBox::Warning);
            msgBox.setStandardButtons(QMessageBox::Ok);
            msgBox.setText(
                tr("<html><head/><body><p><b>无法创建目标目录</b></p> \
                              <p>请确认你有相关权限\
                              并再次尝试，或指定一个不同的位置。</p></body></html>"));
            msgBox.exec();
            return false;
        }
    }

    QFileInfo info(path);

    if (!info.isWritable())
    {
        QMessageBox msgBox;
        msgBox.setWindowTitle(tr("权限不足"));
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setText(
            tr("<html><head/><body><p><b>无法写入目标目录</b></p> \
                          <p>请确认你有相关权限\
                          并再次尝试，或指定一个不同的位置。</p></body></html>"));
        msgBox.exec();
        return false;
    }

    if (mWizard->findFiles(QLatin1String("Morrowind"), path))
    {
        QMessageBox msgBox;
        msgBox.setWindowTitle(tr("目标不为空"));
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setText(
            tr("<html><head/><body><p><b>目标目录不是空的</b></p> \
                          <p>在指定位置找到了已安装的晨风。</p> \
                          <p>请指定一个不同的位置，或者回到上一步并按已安装的方式选择这个位置。</p></body></html>"));
        msgBox.exec();
        return false;
    }

    return true;
}

void Wizard::InstallationTargetPage::on_browseButton_clicked()
{
    QString selectedPath = QFileDialog::getExistingDirectory(this, tr("选择把晨风安装到哪"),
        QDir::homePath(), QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    qDebug() << selectedPath;
    QFileInfo info(selectedPath);
    if (!info.exists())
        return;

    if (info.isWritable())
        targetLineEdit->setText(info.absoluteFilePath());
}

int Wizard::InstallationTargetPage::nextId() const
{
    return MainWizard::Page_LanguageSelection;
}
