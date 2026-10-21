#include "preferences/dialog/dlgprefbeats.h"

#include <QFileDialog>
#include <QSignalBlocker>

#include "analyzer/analyzerbeats.h"
#include "analyzer/plugins/analyzerbeatthis.h"
#include "analyzer/plugins/analyzercustombeats.h"
#include "analyzer/plugins/analyzerqueenmarybeats.h"
#include "defs_urls.h"
#include "moc_dlgprefbeats.cpp"

DlgPrefBeats::DlgPrefBeats(QWidget* parent, UserSettingsPointer pConfig)
        : DlgPreferencePage(parent),
          m_pConfig(pConfig),
          m_bpmSettings(pConfig),
          m_bAnalyzerEnabled(m_bpmSettings.getBpmDetectionEnabledDefault()),
          m_tempoMode(m_bpmSettings.getTempoModeDefault()),
          m_bFastAnalysisEnabled(m_bpmSettings.getFastAnalysisDefault()),
          m_bReanalyze(m_bpmSettings.getReanalyzeWhenSettingsChangeDefault()),
          m_bReanalyzeImported(m_bpmSettings.getReanalyzeImportedDefault()),
          m_stemStrategy(BeatDetectionSettings::StemStrategy::Disabled) {
    setupUi(this);
    comboBoxTempoMode->addItem(
            tr("Auto"), static_cast<int>(mixxx::BeatGridMode::Auto));
    comboBoxTempoMode->addItem(
            tr("Constant"), static_cast<int>(mixxx::BeatGridMode::Constant));
    comboBoxTempoMode->addItem(
            tr("Variable"), static_cast<int>(mixxx::BeatGridMode::Variable));

    m_availablePlugins = AnalyzerBeats::availablePlugins();
    for (const auto& info : std::as_const(m_availablePlugins)) {
        comboBoxBeatPlugin->addItem(info.name(), info.id());
        if (info.id() == mixxx::AnalyzerQueenMaryBeats::pluginInfo().id() ||
                info.id() == mixxx::AnalyzerBeatThis::pluginInfo().id()) {
            comboBoxCustomConstant->addItem(info.name(), info.id());
            comboBoxCustomVariable->addItem(info.name(), info.id());
        }
    }

    connect(buttonBeatThisPython, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this,
                tr("Choose Python interpreter"),
                lineEditBeatThisPython->text());
        if (!path.isEmpty()) {
            lineEditBeatThisPython->setText(path);
        }
    });
    connect(buttonBeatThisModel, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this,
                tr("Choose Beat This! model"),
                lineEditBeatThisModel->text(),
                tr("Model checkpoints (*.ckpt);;All files (*)"));
        if (!path.isEmpty()) {
            lineEditBeatThisModel->setText(path);
        }
    });
    slotUpdate();

    // TODO (#13466) Keeping the setting hidden for now
    comboBoxStemStrategy->hide();
    labelStemStrategy->hide();

    // Connections
    connect(comboBoxBeatPlugin,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &DlgPrefBeats::pluginSelected);
    connect(comboBoxCustomConstant, &QComboBox::currentIndexChanged, this, [this] {
        m_customConstantId = comboBoxCustomConstant->currentData().toString();
        updateGui();
    });
    connect(comboBoxCustomVariable, &QComboBox::currentIndexChanged, this, [this] {
        m_customVariableId = comboBoxCustomVariable->currentData().toString();
        updateGui();
    });
    connect(checkBoxAnalyzerEnabled,
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
            &QCheckBox::checkStateChanged,
#else
            &QCheckBox::stateChanged,
#endif
            this,
            &DlgPrefBeats::analyzerEnabled);
    connect(comboBoxTempoMode,
            &QComboBox::currentIndexChanged,
            this,
            &DlgPrefBeats::tempoModeSelected);
    connect(checkBoxFastAnalysis,
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
            &QCheckBox::checkStateChanged,
#else
            &QCheckBox::stateChanged,
#endif
            this,
            &DlgPrefBeats::fastAnalysisEnabled);
    connect(checkBoxReanalyze,
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
            &QCheckBox::checkStateChanged,
#else
            &QCheckBox::stateChanged,
#endif
            this,
            &DlgPrefBeats::slotReanalyzeChanged);
    connect(checkBoxReanalyzeImported,
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
            &QCheckBox::checkStateChanged,
#else
            &QCheckBox::stateChanged,
#endif
            this,
            &DlgPrefBeats::slotReanalyzeImportedChanged);
    connect(comboBoxStemStrategy,
            &QComboBox::currentIndexChanged,
            this,
            &DlgPrefBeats::slotStemStrategyChanged);

    setScrollSafeGuard(comboBoxBeatPlugin);
    setScrollSafeGuard(comboBoxCustomConstant);
    setScrollSafeGuard(comboBoxCustomVariable);
}

DlgPrefBeats::~DlgPrefBeats() {
}

QUrl DlgPrefBeats::helpUrl() const {
    return QUrl(MIXXX_MANUAL_BEATS_URL);
}

void DlgPrefBeats::slotResetToDefaults() {
    if (m_availablePlugins.size() > 0) {
        m_selectedAnalyzerId = m_availablePlugins[0].id();
    }
    m_bAnalyzerEnabled = m_bpmSettings.getBpmDetectionEnabledDefault();
    m_tempoMode = m_bpmSettings.getTempoModeDefault();
    m_bFastAnalysisEnabled = m_bpmSettings.getFastAnalysisDefault();
    m_bReanalyze = m_bpmSettings.getReanalyzeWhenSettingsChangeDefault();
    m_bReanalyzeImported = m_bpmSettings.getReanalyzeImportedDefault();
    m_stemStrategy = m_bpmSettings.getStemStrategyDefault();
    m_customConstantId = m_bpmSettings.getCustomConstantPluginDefault();
    m_customVariableId = m_bpmSettings.getCustomVariablePluginDefault();
    checkBoxQueenMaryDownbeats->setChecked(m_bpmSettings.getQueenMaryDownbeatsDefault());
    lineEditBeatThisPython->setText(m_bpmSettings.getBeatThisPythonDefault());
    lineEditBeatThisModel->setText(m_bpmSettings.getBeatThisModelDefault());
    lineEditBeatThisHash->setText(
            m_bpmSettings.getBeatThisModelSha256Default());

    updateGui();
}

void DlgPrefBeats::pluginSelected(int i) {
    if (i < 0 || i >= m_availablePlugins.size()) {
        return;
    }
    m_selectedAnalyzerId = m_availablePlugins[i].id();
    updateGui();
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
void DlgPrefBeats::analyzerEnabled(Qt::CheckState state) {
    m_bAnalyzerEnabled = (state == Qt::Checked);
#else
void DlgPrefBeats::analyzerEnabled(int i) {
    m_bAnalyzerEnabled = static_cast<bool>(i);
#endif
    updateGui();
}

void DlgPrefBeats::tempoModeSelected(int i) {
    if (i < 0 || i >= comboBoxTempoMode->count()) {
        return;
    }
    m_tempoMode = static_cast<mixxx::BeatGridMode>(
            comboBoxTempoMode->itemData(i).toInt());
    updateGui();
}

void DlgPrefBeats::slotUpdate() {
    // Read true values from config
    m_selectedAnalyzerId = m_bpmSettings.getBeatPluginId();
    m_bAnalyzerEnabled = m_bpmSettings.getBpmDetectionEnabled();
    m_tempoMode = m_bpmSettings.getTempoMode();
    m_bReanalyze = m_bpmSettings.getReanalyzeWhenSettingsChange();
    m_bReanalyzeImported = m_bpmSettings.getReanalyzeImported();
    m_bFastAnalysisEnabled = m_bpmSettings.getFastAnalysis();
    m_stemStrategy = m_bpmSettings.getStemStrategy();
    m_customConstantId = m_bpmSettings.getCustomConstantPlugin();
    m_customVariableId = m_bpmSettings.getCustomVariablePlugin();
    checkBoxQueenMaryDownbeats->setChecked(m_bpmSettings.getQueenMaryDownbeats());
    lineEditBeatThisPython->setText(m_bpmSettings.getBeatThisPython());
    lineEditBeatThisModel->setText(m_bpmSettings.getBeatThisModel());
    lineEditBeatThisHash->setText(m_bpmSettings.getBeatThisModelSha256());

    updateGui();
}

void DlgPrefBeats::updateGui() {
    const QSignalBlocker pluginBlocker(comboBoxBeatPlugin);
    const QSignalBlocker customConstantBlocker(comboBoxCustomConstant);
    const QSignalBlocker customVariableBlocker(comboBoxCustomVariable);
    const QSignalBlocker enabledBlocker(checkBoxAnalyzerEnabled);
    const QSignalBlocker tempoModeBlocker(comboBoxTempoMode);
    const QSignalBlocker fastAnalysisBlocker(checkBoxFastAnalysis);
    const QSignalBlocker reanalyzeBlocker(checkBoxReanalyze);
    const QSignalBlocker importedBlocker(checkBoxReanalyzeImported);
    const QSignalBlocker stemBlocker(comboBoxStemStrategy);
    const bool custom = m_selectedAnalyzerId == mixxx::AnalyzerCustomBeats::pluginInfo().id();
    const auto setChoice = [](QComboBox* box, QString* id, const QString& fallback) {
        int index = box->findData(*id);
        if (index < 0) {
            *id = fallback;
            index = box->findData(*id);
        }
        box->setCurrentIndex(index);
    };
    setChoice(comboBoxCustomConstant, &m_customConstantId, m_bpmSettings.getCustomConstantPluginDefault());
    setChoice(comboBoxCustomVariable, &m_customVariableId, m_bpmSettings.getCustomVariablePluginDefault());
    groupBoxCustomAnalysis->setVisible(custom);
    groupBoxCustomAnalysis->setEnabled(m_bAnalyzerEnabled);
    const auto beatThisId = mixxx::AnalyzerBeatThis::pluginInfo().id();
    groupBoxBeatThis->setVisible(m_selectedAnalyzerId == beatThisId ||
            (custom && (m_customConstantId == beatThisId || m_customVariableId == beatThisId)));
    groupBoxBeatThis->setEnabled(m_bAnalyzerEnabled);
    const auto queenMaryId = mixxx::AnalyzerQueenMaryBeats::pluginInfo().id();
    checkBoxQueenMaryDownbeats->setVisible(m_selectedAnalyzerId == queenMaryId ||
            (custom && (m_customConstantId == queenMaryId || m_customVariableId == queenMaryId)));
    checkBoxQueenMaryDownbeats->setEnabled(m_bAnalyzerEnabled);
    comboBoxTempoMode->setEnabled(m_bAnalyzerEnabled);
    comboBoxBeatPlugin->setEnabled(m_bAnalyzerEnabled);
    checkBoxAnalyzerEnabled->setChecked(m_bAnalyzerEnabled);
    // Fast analysis cannot be combined with non-constant tempo beatgrids.
    checkBoxFastAnalysis->setEnabled(
            m_bAnalyzerEnabled && m_tempoMode == mixxx::BeatGridMode::Constant);
    checkBoxReanalyze->setEnabled(m_bAnalyzerEnabled);
    checkBoxReanalyzeImported->setEnabled(m_bAnalyzerEnabled);

    if (m_availablePlugins.size() > 0) {
        bool found = false;
        for (int i = 0; i < m_availablePlugins.size(); ++i) {
            const auto& info = m_availablePlugins.at(i);
            if (info.id() == m_selectedAnalyzerId) {
                found = true;
                comboBoxBeatPlugin->setCurrentIndex(i);
                if (!m_availablePlugins[i].isConstantTempoSupported()) {
                    comboBoxTempoMode->setEnabled(false);
                }
                break;
            }
        }
        if (!found) {
            comboBoxBeatPlugin->setCurrentIndex(0);
            m_selectedAnalyzerId = m_availablePlugins[0].id();
        }
    }

    comboBoxTempoMode->setCurrentIndex(
            comboBoxTempoMode->findData(static_cast<int>(m_tempoMode)));
    // Fast analysis cannot be combined with non-constant tempo beatgrids.
    checkBoxFastAnalysis->setChecked(m_bFastAnalysisEnabled &&
            m_tempoMode == mixxx::BeatGridMode::Constant);

    checkBoxReanalyze->setChecked(m_bReanalyze);
    checkBoxReanalyzeImported->setChecked(m_bReanalyzeImported);

    comboBoxStemStrategy->setCurrentIndex(
            m_stemStrategy == BeatDetectionSettings::StemStrategy::Enforced
                    ? 1
                    : 0);
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
void DlgPrefBeats::slotReanalyzeChanged(Qt::CheckState state) {
    m_bReanalyze = (state == Qt::Checked);
#else
void DlgPrefBeats::slotReanalyzeChanged(int value) {
    m_bReanalyze = static_cast<bool>(value);
#endif
    updateGui();
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
void DlgPrefBeats::slotReanalyzeImportedChanged(Qt::CheckState state) {
    m_bReanalyzeImported = (state == Qt::Checked);
#else
void DlgPrefBeats::slotReanalyzeImportedChanged(int value) {
    m_bReanalyzeImported = static_cast<bool>(value);
#endif
    updateGui();
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
void DlgPrefBeats::fastAnalysisEnabled(Qt::CheckState state) {
    m_bFastAnalysisEnabled = (state == Qt::Checked);
#else
void DlgPrefBeats::fastAnalysisEnabled(int i) {
    m_bFastAnalysisEnabled = static_cast<bool>(i);
#endif
    updateGui();
}

void DlgPrefBeats::slotStemStrategyChanged(int index) {
    switch (index) {
    case 1:
        m_stemStrategy = BeatDetectionSettings::StemStrategy::Enforced;
        break;
    default:
        m_stemStrategy = BeatDetectionSettings::StemStrategy::Disabled;
        break;
    }
    updateGui();
}

void DlgPrefBeats::slotApply() {
    m_bpmSettings.setBeatPluginId(m_selectedAnalyzerId);
    m_bpmSettings.setCustomConstantPlugin(m_customConstantId);
    m_bpmSettings.setCustomVariablePlugin(m_customVariableId);
    m_bpmSettings.setQueenMaryDownbeats(checkBoxQueenMaryDownbeats->isChecked());
    m_bpmSettings.setBpmDetectionEnabled(m_bAnalyzerEnabled);
    m_bpmSettings.setTempoMode(m_tempoMode);
    m_bpmSettings.setReanalyzeWhenSettingsChange(m_bReanalyze);
    m_bpmSettings.setReanalyzeImported(m_bReanalyzeImported);
    m_bpmSettings.setFastAnalysis(m_bFastAnalysisEnabled);
    m_bpmSettings.setStemStrategy(m_stemStrategy);
    m_bpmSettings.setBeatThisPython(lineEditBeatThisPython->text().trimmed());
    m_bpmSettings.setBeatThisModel(lineEditBeatThisModel->text().trimmed());
    m_bpmSettings.setBeatThisModelSha256(
            lineEditBeatThisHash->text().trimmed().toLower());
    if (!m_pConfig->save()) {
        qWarning() << "Failed to save beat detection preferences";
    }
}
