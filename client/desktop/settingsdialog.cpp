#include "settingsdialog.h"
#include "mainwindow.h"
#include "keyboardlayouttester.h"

#include <QCheckBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>

#include <QPushButton>
#include <QLineEdit>
#include <QSpinBox>
#include <QFileDialog>
#include <QDir>
#include <QMessageBox>
#include <QStandardPaths>

#include "entryiconprovider.h"
#include "signetapplication.h"

#include "style.h"

SettingsDialog::SettingsDialog(MainWindow *mainWindow, bool initial) :
	QDialog(mainWindow),
	m_initial(initial)
{
	m_settings = mainWindow->getSettings();
	setWindowTitle("System settings");

	QPushButton *okayButton = new QPushButton("&Save");
	connect(okayButton, SIGNAL(pressed()), this, SLOT(okayPressed()));

	QPushButton *cancelButton = new QPushButton("Cancel");
	connect(cancelButton, SIGNAL(pressed()), this, SLOT(cancelPressed()));

	m_activeKeyboardLayout = m_settings->activeKeyboardLayout;
	m_keyboardLayouts = m_settings->keyboardLayouts;

	m_localBackups = new QCheckBox("Enable &local backups");
	m_localBackups->setChecked(m_settings->localBackups);

	m_localBackupPath = new QLineEdit(m_settings->localBackupPath);
	m_localBackupPath->setMinimumWidth(300);

	m_localBackupInterval = new QSpinBox();
	m_localBackupInterval->setSuffix(" days");
	m_localBackupInterval->setMinimum(1);
	m_localBackupInterval->setMaximum(60);
	m_localBackupInterval->setValue(m_settings->localBackupInterval);

	m_removableBackups = new QCheckBox("Enable &removable backups");
	m_removableBackups->setChecked(m_settings->removableBackups);

	m_removableBackupVolume = new QLineEdit(m_settings->removableBackupVolume);
	m_removableBackupDirectory = new QLineEdit(m_settings->removableBackupPath);

	m_removableBackupInterval = new QSpinBox();
	m_removableBackupInterval->setSuffix(" days");
	m_removableBackupInterval->setMinimum(1);
	m_removableBackupInterval->setMaximum(60);
	m_removableBackupInterval->setValue(m_settings->removableBackupInterval);

	QPushButton *localBackupPathBrowseButton = new QPushButton("Browse");
	connect(localBackupPathBrowseButton, SIGNAL(pressed()), this, SLOT(localBackupPathBrowse()));

	QLayout *buttonLayout = new QHBoxLayout();
	buttonLayout->setAlignment(Qt::AlignRight);
	buttonLayout->addWidget(okayButton);
	buttonLayout->addWidget(cancelButton);

	QHBoxLayout *localBackupPathLayout = new QHBoxLayout();
	localBackupPathLayout->addWidget(new genericText("Local backup path"));
	localBackupPathLayout->addWidget(m_localBackupPath);
	localBackupPathLayout->addWidget(localBackupPathBrowseButton);

	QHBoxLayout *localBackupIntervalLayout = new QHBoxLayout();
	localBackupIntervalLayout->addWidget(new genericText("Local backup every"));
	localBackupIntervalLayout->addWidget(m_localBackupInterval);

	QHBoxLayout *removableBackupVolumeLayout = new QHBoxLayout();
	removableBackupVolumeLayout->addWidget(new genericText("Removeable backup volume label"));
	removableBackupVolumeLayout->addWidget(m_removableBackupVolume);

	QHBoxLayout *removableBackupDirectoryLayout = new QHBoxLayout();
	removableBackupDirectoryLayout->addWidget(new genericText("Removeable backup directory name"));
	removableBackupDirectoryLayout->addWidget(m_removableBackupDirectory);

	QHBoxLayout *removableBackupIntervalLayout = new QHBoxLayout();
	removableBackupIntervalLayout->addWidget(new genericText("Removable backup every"));
	removableBackupIntervalLayout->addWidget(m_removableBackupInterval);

	bool keyboardLayoutConfigured = !m_settings->keyboardLayouts.isEmpty();
	QHBoxLayout *keyboardLayoutConfigurationLayout = new QHBoxLayout();
	if (keyboardLayoutConfigured) {
		keyboardLayoutConfigurationLayout->addWidget(new genericText("Keyboard layout"));
		m_configureKeyboardLayout = new QPushButton("Re&configure");
	} else {
		keyboardLayoutConfigurationLayout->addWidget(new genericText("Keyboard layout"));
		m_configureKeyboardLayout = new QPushButton("&Configure");
	}
	keyboardLayoutConfigurationLayout->addWidget(m_configureKeyboardLayout);
	connect(m_configureKeyboardLayout, SIGNAL(pressed()),
		this, SLOT(configureKeyboardLayout()));

	m_keyboardLayoutUnconfiguredWarning = new emphasisText(
        "Note: Signet's keyboard layout is not configured. Signet will default to a English(US) keyboard layout which will cause incorrect keys to be generated if this system uses a different layout.");
	m_keyboardLayoutUnconfiguredWarning->setWordWrap(true);
	if (keyboardLayoutConfigured) {
		m_keyboardLayoutUnconfiguredWarning->hide();
	}

	// Icon pack used for the small pictures next to each entry
	m_iconPackLabel = new genericText(QString());
	m_iconPackLabel->setWordWrap(true);
	QPushButton *importIconPackButton = new QPushButton("&Import icon pack...");
	connect(importIconPackButton, SIGNAL(pressed()), this, SLOT(importIconPack()));
	m_removeIconPackButton = new QPushButton("Use &built-in icons");
	connect(m_removeIconPackButton, SIGNAL(pressed()), this, SLOT(removeIconPack()));
	QHBoxLayout *iconPackLayout = new QHBoxLayout();
	iconPackLayout->addWidget(m_iconPackLabel, 1);
	iconPackLayout->addWidget(importIconPackButton);
	iconPackLayout->addWidget(m_removeIconPackButton);
	updateIconPackInfo();

#ifndef Q_OS_MACOS
	m_minimizeToTray = new QCheckBox("&Minimize window to system tray");
	m_minimizeToTray->setChecked(m_settings->minimizeToTray);
#else
	m_minimizeToTray = nullptr;
#endif

	QVBoxLayout *topLayout = new QVBoxLayout();
	topLayout->setAlignment(Qt::AlignTop);
	topLayout->addWidget(m_localBackups);
	topLayout->addLayout(localBackupPathLayout);
	topLayout->addLayout(localBackupIntervalLayout);
	topLayout->addWidget(m_removableBackups);
	topLayout->addLayout(removableBackupVolumeLayout);
	topLayout->addLayout(removableBackupDirectoryLayout);
	topLayout->addLayout(removableBackupIntervalLayout);
	topLayout->addWidget(m_keyboardLayoutUnconfiguredWarning);
	topLayout->addLayout(keyboardLayoutConfigurationLayout);
	topLayout->addLayout(iconPackLayout);
#ifndef Q_OS_MACOS
	topLayout->addWidget(m_minimizeToTray);
#endif
	topLayout->addLayout(buttonLayout);
	setLayout(topLayout);
	setEnableDisable();
	connect(m_localBackups, SIGNAL(clicked(bool)), this, SLOT(setEnableDisable()));
	connect(m_removableBackups, SIGNAL(clicked(bool)), this, SLOT(setEnableDisable()));
}

void SettingsDialog::configureKeyboardLayout()
{
	QVector<struct signetdev_key> currentLayout;

	int n_keys;
	const signetdev_key *keymap = ::signetdev_get_keymap(&n_keys);
	for (int i = 0; i < n_keys; i++) {
		currentLayout.append(keymap[i]);
	}

	m_keyboardLayoutTester = new KeyboardLayoutTester(currentLayout, this);
	connect(m_keyboardLayoutTester, SIGNAL(closing(bool)),
		this, SLOT(keyboardLayoutTesterClosing(bool)));
	connect(m_keyboardLayoutTester, SIGNAL(applyChanges()),
		this, SLOT(applyKeyboardLayoutChanges()));
	m_keyboardLayoutTester->setWindowModality(Qt::WindowModal);
	m_keyboardLayoutTester->show();
}

void SettingsDialog::applyKeyboardLayoutChanges()
{
	auto keyboardLayout = m_keyboardLayoutTester->getLayout();
	::signetdev_set_keymap(keyboardLayout.data(), keyboardLayout.size());
	m_activeKeyboardLayout = QString("current");
	m_keyboardLayouts.insert("current", keyboardLayout);
	m_keyboardLayoutUnconfiguredWarning->hide();
	m_configureKeyboardLayout->setText("Reconfigure");
}

void SettingsDialog::keyboardLayoutTesterClosing(bool applyChanges)
{
	if (applyChanges) {
		applyKeyboardLayoutChanges();
	}
	m_keyboardLayoutTester->deleteLater();
}


void SettingsDialog::setEnableDisable()
{
	bool enableLocal = m_localBackups->isChecked();
	m_localBackupPath->setEnabled(enableLocal);
	m_localBackupInterval->setEnabled(enableLocal);

	bool enableRemovable = m_removableBackups->isChecked();
	m_removableBackupVolume->setEnabled(enableRemovable);
	m_removableBackupDirectory->setEnabled(enableRemovable);
	m_removableBackupInterval->setEnabled(enableRemovable);
}

void SettingsDialog::okayPressed()
{
	m_settings->localBackups = m_localBackups->isChecked();
	m_settings->localBackupPath = m_localBackupPath->text();
	m_settings->localBackupInterval = m_localBackupInterval->value();
	m_settings->removableBackups = m_removableBackups->isChecked();
	m_settings->removableBackupPath = m_removableBackupDirectory->text();
	m_settings->removableBackupVolume = m_removableBackupVolume->text();
	m_settings->removableBackupInterval = m_removableBackupInterval->value();
	m_settings->activeKeyboardLayout = m_activeKeyboardLayout;
	m_settings->keyboardLayouts = m_keyboardLayouts;
#ifndef Q_OS_MACOS
	m_settings->minimizeToTray = m_minimizeToTray->isChecked();
#endif
	done(0);
}

void SettingsDialog::cancelPressed()
{
	done(1);
}

void SettingsDialog::localBackupPathBrowse()
{
	QDir dir(m_localBackupPath->text());
	QFileDialog *fd = new QFileDialog(this, "Local backup directory");
	fd->setDirectory(dir);
	fd->setFileMode(QFileDialog::Directory);
	if (!fd->exec()) {
		fd->deleteLater();
		return;
	}
	QStringList sl = fd->selectedFiles();
	if (sl.empty()) {
		fd->deleteLater();
		return;
	}
	m_localBackupPath->setText(sl.first());
	fd->deleteLater();
}

void SettingsDialog::updateIconPackInfo()
{
	EntryIconProvider::PackInfo pack = EntryIconProvider::activePack();
	QString name = pack.name.isEmpty() ? QString("unnamed pack") : pack.name;
	QString text = QString("Entry icons: %1%2, %3 icons%4")
		.arg(name)
		.arg(pack.version ? QString(" version %1").arg(pack.version) : QString())
		.arg(pack.iconCount)
		.arg(pack.imported ? QString(" (imported)") : QString(" (built in)"));
	m_iconPackLabel->setText(text);
	m_removeIconPackButton->setVisible(pack.imported);
}

void SettingsDialog::importIconPack()
{
	QString file = QFileDialog::getOpenFileName(this, "Import icon pack",
				QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
				"Icon pack (*.zip)");
	if (file.isEmpty()) {
		return;
	}
	QString error;
	if (!EntryIconProvider::importPack(file, &error)) {
		SignetApplication::messageBoxError(QMessageBox::Warning, "Import icon pack",
			"This icon pack could not be imported. " + error, this);
		return;
	}
	updateIconPackInfo();
	SignetApplication::messageBoxError(QMessageBox::Information, "Import icon pack",
		"The icon pack was imported and is now used for your entries.", this);
}

void SettingsDialog::removeIconPack()
{
	QString error;
	if (!EntryIconProvider::removeImportedPack(&error)) {
		SignetApplication::messageBoxError(QMessageBox::Warning, "Remove icon pack", error, this);
		return;
	}
	updateIconPackInfo();
}
