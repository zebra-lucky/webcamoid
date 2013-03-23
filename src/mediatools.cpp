/* Webcamod, webcam capture plasmoid.
 * Copyright (C) 2011-2012  Gonzalo Exequiel Pedone
 *
 * Webcamod is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Webcamod is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Webcamod. If not, see <http://www.gnu.org/licenses/>.
 *
 * Email     : hipersayan DOT x AT gmail DOT com
 * Web-Site 1: http://github.com/hipersayanX/Webcamoid
 * Web-Site 2: http://kde-apps.org/content/show.php/Webcamoid?content=144796
 */

#include <sys/ioctl.h>
#include <QtXml>
#include <KSharedConfig>
#include <KConfigGroup>
#include <linux/videodev2.h>

#include "mediatools.h"

MediaTools::MediaTools(bool watchDevices, QObject *parent): QObject(parent)
{
    this->m_appEnvironment = new AppEnvironment(this);

    QObject::connect(QCoreApplication::instance(),
                     SIGNAL(aboutToQuit()),
                     this,
                     SLOT(aboutToQuit()));

    this->resetDevice();
    this->resetVideoFormat();
    this->resetEffectsPreview();
    this->resetRecordAudio();
    this->resetRecording();
    this->resetVideoRecordFormats();
    this->resetStreams();
    this->resetWindowSize();

    Qb::init();

    Qb::setPluginsPaths(Qb::pluginsPaths() << "Qb/Plugins/ACapsConvert"
                                           << "Qb/Plugins/Bin"
                                           << "Qb/Plugins/Blitzer"
                                           << "Qb/Plugins/Filter"
                                           << "Qb/Plugins/Frei0r"
                                           << "Qb/Plugins/MultiSink"
                                           << "Qb/Plugins/MultiSrc"
                                           << "Qb/Plugins/Multiplex"
                                           << "Qb/Plugins/Probe"
                                           << "Qb/Plugins/QImageConvert"
                                           << "Qb/Plugins/Sync"
                                           << "Qb/Plugins/VCapsConvert");

// MultiSink objectName='audioOutput' location='pulse' options='-vn -ac 2 -f alsa'

    this->m_pipeline = Qb::create("Bin", "pipeline");

    this->m_pipeline->setProperty("description",
                                  "MultiSrc objectName='source' loop=true "
                                  "stateChanged>videoMux.setState "
                                  "stateChanged>effects.setState "
                                  "stateChanged>videoSync.setState "
                                  "stateChanged>videoOutput.setState !"
                                  "Multiplex objectName='videoMux' "
                                  "caps='video/x-raw' outputIndex=0 !"
                                  "Bin objectName='effects' blocking=false !"
                                  "Sync objectName='videoSync' !"
                                  "QImageConvert objectName='videoOutput' ! "
                                  "OUT. ,"
                                  "source. !"
                                  "Multiplex caps='audio/x-raw' outputIndex=0 !"
                                  "Multiplex objectName='audioSwitch' "
                                  "outputIndex=1 !"
                                  "MultiSink objectName='audioOutput' ,"
                                  "MultiSrc objectName='mic' !"
                                  "Multiplex outputIndex=1 ! audioSwitch. ,"
                                  "effects. ! MultiSink objectName='record' ,"
                                  "audioSwitch. ! record.");

    this->m_effectsPreview = Qb::create("Bin");

    QMetaObject::invokeMethod(this->m_pipeline.data(),
                              "element",
                              Q_RETURN_ARG(QbElementPtr, this->m_source),
                              Q_ARG(QString, "source"));

    QMetaObject::invokeMethod(this->m_pipeline.data(),
                              "element",
                              Q_RETURN_ARG(QbElementPtr, this->m_effects),
                              Q_ARG(QString, "effects"));

    QMetaObject::invokeMethod(this->m_pipeline.data(),
                              "element",
                              Q_RETURN_ARG(QbElementPtr, this->m_audioSwitch),
                              Q_ARG(QString, "audioSwitch"));

    QMetaObject::invokeMethod(this->m_pipeline.data(),
                              "element",
                              Q_RETURN_ARG(QbElementPtr, this->m_audioOutput),
                              Q_ARG(QString, "audioOutput"));

    QMetaObject::invokeMethod(this->m_pipeline.data(),
                              "element",
                              Q_RETURN_ARG(QbElementPtr, this->m_mic),
                              Q_ARG(QString, "mic"));

    QMetaObject::invokeMethod(this->m_pipeline.data(),
                              "element",
                              Q_RETURN_ARG(QbElementPtr, this->m_record),
                              Q_ARG(QString, "record"));

    this->m_pipeline->link(this);
    this->m_source->link(this->m_effectsPreview);

    if (watchDevices)
    {
        this->m_fsWatcher = new QFileSystemWatcher(QStringList() << "/dev", this);

        QObject::connect(this->m_fsWatcher,
                         SIGNAL(directoryChanged(const QString &)),
                         this,
                         SLOT(onDirectoryChanged(const QString &)));
    }

    this->loadConfigs();
}

MediaTools::~MediaTools()
{
    this->resetDevice();
    this->saveConfigs();
}

void MediaTools::iStream(const QbPacket &packet)
{
    QString sender = this->sender()->objectName();
    const QImage *frame = static_cast<const QImage *>(packet.data());

    if (sender == "pipeline")
    {
        emit this->frameReady(*frame);

        if (frame->size() != this->m_curFrameSize)
        {
            emit this->frameSizeChanged(frame->size());
            this->m_curFrameSize = frame->size();
        }
    }
    else
        emit this->previewFrameReady(*frame, sender);
}

QString MediaTools::device()
{
    return this->m_device;
}

QVariantList MediaTools::videoFormat(QString device)
{
    QFile deviceFile(device.isEmpty()? this->device(): device);
    QVariantList format;

    if (!deviceFile.open(QIODevice::ReadWrite | QIODevice::Unbuffered))
        return format;

    v4l2_format fmt;
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (ioctl(deviceFile.handle(), VIDIOC_G_FMT, &fmt) >= 0)
        format << fmt.fmt.pix.width
               << fmt.fmt.pix.height
               << fmt.fmt.pix.pixelformat;

    deviceFile.close();

    return format;
}

bool MediaTools::effectsPreview()
{
    return this->m_showEffectsPreview;
}

bool MediaTools::recordAudio()
{
    return this->m_recordAudio;
}

bool MediaTools::recording()
{
    return this->m_recording;
}

QList<QStringList> MediaTools::videoRecordFormats()
{
    return this->m_videoRecordFormats;
}

QVariantList MediaTools::streams()
{
    return this->m_streams;
}

QSize MediaTools::windowSize()
{
    return this->m_windowSize;
}

QString MediaTools::fcc2s(uint val)
{
    QString s = "";

    s += QChar(val & 0xff);
    s += QChar((val >> 8) & 0xff);
    s += QChar((val >> 16) & 0xff);
    s += QChar((val >> 24) & 0xff);

    return s;
}

QVariantList MediaTools::videoFormats(QString device)
{
    QFile deviceFile(device);
    QVariantList formats;

    if (!deviceFile.open(QIODevice::ReadWrite | QIODevice::Unbuffered))
        return formats;

    QList<v4l2_buf_type> bufType;

    bufType << V4L2_BUF_TYPE_VIDEO_CAPTURE
            << V4L2_BUF_TYPE_VIDEO_OUTPUT
            << V4L2_BUF_TYPE_VIDEO_OVERLAY;

    foreach (v4l2_buf_type type, bufType)
    {
        v4l2_fmtdesc fmt;
        fmt.index = 0;
        fmt.type = type;

        while (ioctl(deviceFile.handle(), VIDIOC_ENUM_FMT, &fmt) >= 0)
        {
            v4l2_frmsizeenum frmsize;
            frmsize.pixel_format = fmt.pixelformat;
            frmsize.index = 0;

            while (ioctl(deviceFile.handle(),
                         VIDIOC_ENUM_FRAMESIZES,
                         &frmsize) >= 0)
            {
                if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE)
                {
                    QVariantList format;

                    format << frmsize.discrete.width
                           << frmsize.discrete.height
                           << fmt.pixelformat;

                    formats << QVariant(format);
                }

                frmsize.index++;
            }

            fmt.index++;
        }
    }

    deviceFile.close();

    return formats;
}

QVariantList MediaTools::captureDevices()
{
    QVariantList webcamsDevices;
    QDir devicesDir("/dev");

    QStringList devices = devicesDir.entryList(QStringList() << "video*",
                                               QDir::System |
                                               QDir::Readable |
                                               QDir::Writable |
                                               QDir::NoSymLinks |
                                               QDir::NoDotAndDotDot |
                                               QDir::CaseSensitive,
                                               QDir::Name);

    QFile device;
    v4l2_capability capability;

    foreach (QString devicePath, devices)
    {
        device.setFileName(devicesDir.absoluteFilePath(devicePath));

        if (device.open(QIODevice::ReadWrite))
        {
            ioctl(device.handle(), VIDIOC_QUERYCAP, &capability);

            if (capability.capabilities & V4L2_CAP_VIDEO_CAPTURE)
            {
                QVariantList cap;

                cap << device.fileName()
                    << QString((const char *) capability.card)
                    << StreamTypeWebcam;

                webcamsDevices << QVariant(cap);
            }

            device.close();
        }
    }

    this->m_webcams = webcamsDevices;

    QVariantList desktopDevice = QVariantList() << "desktop"
                                                << this->tr("Desktop")
                                                << StreamTypeDesktop;

    QVariantList allDevices = webcamsDevices +
                              this->m_streams +
                              QVariantList() << QVariant(desktopDevice);

    return allDevices;
}

QVariantList MediaTools::listControls(QString dev_name)
{
    v4l2_queryctrl queryctrl;
    queryctrl.id = V4L2_CTRL_FLAG_NEXT_CTRL;
    QVariantList controls;

    QFile device(dev_name);

    if (!device.open(QIODevice::ReadWrite | QIODevice::Unbuffered))
        return controls;

    while (ioctl(device.handle(), VIDIOC_QUERYCTRL, &queryctrl) == 0)
    {
        QVariantList control = this->queryControl(device.handle(), &queryctrl);

        if (!control.isEmpty())
            controls << QVariant(control);

        queryctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
    }

    if (queryctrl.id != V4L2_CTRL_FLAG_NEXT_CTRL)
    {
        device.close();

        return controls;
    }

    for (int id = V4L2_CID_USER_BASE; id < V4L2_CID_LASTP1; id++)
    {
        queryctrl.id = id;

        if (ioctl(device.handle(), VIDIOC_QUERYCTRL, &queryctrl) == 0)
        {
            QVariantList control = this->queryControl(device.handle(), &queryctrl);

            if (!control.isEmpty())
                controls << QVariant(control);
        }
    }

    for (queryctrl.id = V4L2_CID_PRIVATE_BASE; ioctl(device.handle(), VIDIOC_QUERYCTRL, &queryctrl) == 0; queryctrl.id++)
    {
        QVariantList control = this->queryControl(device.handle(), &queryctrl);

        if (!control.isEmpty())
            controls << QVariant(control);
    }

    device.close();

    return controls;
}

bool MediaTools::setControls(QString dev_name, QMap<QString, uint> controls)
{
    QFile device(dev_name);

    if (!device.open(QIODevice::ReadWrite | QIODevice::Unbuffered))
        return false;

    QMap<QString, uint> ctrl2id = this->findControls(device.handle());
    std::vector<v4l2_ext_control> mpeg_ctrls;
    std::vector<v4l2_ext_control> user_ctrls;

    foreach (QString control, controls.keys())
    {
        v4l2_ext_control ctrl;
        ctrl.id = ctrl2id[control];
        ctrl.value = controls[control];

        if (V4L2_CTRL_ID2CLASS(ctrl.id) == V4L2_CTRL_CLASS_MPEG)
            mpeg_ctrls.push_back(ctrl);
        else
            user_ctrls.push_back(ctrl);
    }

    foreach (v4l2_ext_control user_ctrl, user_ctrls)
    {
        v4l2_control ctrl;
        ctrl.id = user_ctrl.id;
        ctrl.value = user_ctrl.value;
        ioctl(device.handle(), VIDIOC_S_CTRL, &ctrl);
    }

    if (!mpeg_ctrls.empty())
    {
        v4l2_ext_controls ctrls;
        ctrls.ctrl_class = V4L2_CTRL_CLASS_MPEG;
        ctrls.count = mpeg_ctrls.size();
        ctrls.controls = &mpeg_ctrls[0];
        ioctl(device.handle(), VIDIOC_S_EXT_CTRLS, &ctrls);
    }

    device.close();

    return true;
}

QMap<QString, QString> MediaTools::availableEffects()
{
    QMap<QString, QString> effects;

    QDomDocument effectsXml("effects");
    QFile xmlFile(":/webcamoid/share/effects.xml");
    xmlFile.open(QIODevice::ReadOnly);
    effectsXml.setContent(&xmlFile);
    xmlFile.close();

    QDomNodeList effectNodes = effectsXml.documentElement().childNodes();

    for (int effect = 0; effect < effectNodes.count(); effect++)
    {
        QDomNode effectNode = effectNodes.item(effect);
        QDomNamedNodeMap attributtes = effectNode.attributes();
        QString effectName = attributtes.namedItem("name").nodeValue();
        QString effectDescription = effectNode.firstChild().toText().data();

        effects[effectDescription] = effectName;
    }

    return effects;
}

QStringList MediaTools::currentEffects()
{
    return this->m_effectsList;
}

QString MediaTools::bestRecordFormatOptions(QString fileName)
{
    QString ext = QFileInfo(fileName).completeSuffix();

    if (ext.isEmpty())
        return "";

    foreach (QStringList format, this->m_videoRecordFormats)
        foreach (QString s, format[0].split(",", QString::SkipEmptyParts))
            if (s.toLower().trimmed() == ext)
                return format[1];

    return "";
}

QVariantList MediaTools::queryControl(int dev_fd, v4l2_queryctrl *queryctrl)
{
    if (queryctrl->flags & V4L2_CTRL_FLAG_DISABLED)
        return QVariantList();

    if (queryctrl->type == V4L2_CTRL_TYPE_CTRL_CLASS)
        return QVariantList();

    v4l2_ext_control ext_ctrl;
    ext_ctrl.id = queryctrl->id;

    v4l2_ext_controls ctrls;
    ctrls.ctrl_class = V4L2_CTRL_ID2CLASS(queryctrl->id);
    ctrls.count = 1;
    ctrls.controls = &ext_ctrl;

    if (V4L2_CTRL_ID2CLASS(queryctrl->id) != V4L2_CTRL_CLASS_USER &&
        queryctrl->id < V4L2_CID_PRIVATE_BASE)
    {
        if (ioctl(dev_fd, VIDIOC_G_EXT_CTRLS, &ctrls))
            return QVariantList();
    }
    else
    {
        v4l2_control ctrl;
        ctrl.id = queryctrl->id;

        if (ioctl(dev_fd, VIDIOC_G_CTRL, &ctrl))
            return QVariantList();

        ext_ctrl.value = ctrl.value;
    }

    v4l2_querymenu qmenu;
    qmenu.id = queryctrl->id;
    QStringList menu;

    if (queryctrl->type == V4L2_CTRL_TYPE_MENU)
        for (int i = 0; i < queryctrl->maximum + 1; i++)
        {
            qmenu.index = i;

            if (ioctl(dev_fd, VIDIOC_QUERYMENU, &qmenu))
                continue;

            menu << QString((const char *) qmenu.name);
        }

    return QVariantList() << QString((const char *) queryctrl->name)
                          << queryctrl->type
                          << queryctrl->minimum
                          << queryctrl->maximum
                          << queryctrl->step
                          << queryctrl->default_value
                          << ext_ctrl.value
                          << menu;
}

QMap<QString, uint> MediaTools::findControls(int dev_fd)
{
    v4l2_queryctrl qctrl;
    qctrl.id = V4L2_CTRL_FLAG_NEXT_CTRL;
    QMap<QString, uint> controls;

    while (ioctl(dev_fd, VIDIOC_QUERYCTRL, &qctrl) == 0)
    {
        if (qctrl.type != V4L2_CTRL_TYPE_CTRL_CLASS &&
            !(qctrl.flags & V4L2_CTRL_FLAG_DISABLED))
            controls[QString((const char *) qctrl.name)] = qctrl.id;

        qctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
    }

    if (qctrl.id != V4L2_CTRL_FLAG_NEXT_CTRL)
        return controls;

    for (int id = V4L2_CID_USER_BASE; id < V4L2_CID_LASTP1; id++)
    {
        qctrl.id = id;

        if (ioctl(dev_fd, VIDIOC_QUERYCTRL, &qctrl) == 0 &&
           !(qctrl.flags & V4L2_CTRL_FLAG_DISABLED))
            controls[QString((const char *) qctrl.name)] = qctrl.id;
    }

    qctrl.id = V4L2_CID_PRIVATE_BASE;

    while (ioctl(dev_fd, VIDIOC_QUERYCTRL, &qctrl) == 0)
    {
        if (!(qctrl.flags & V4L2_CTRL_FLAG_DISABLED))
            controls[QString((const char *) qctrl.name)] = qctrl.id;

        qctrl.id++;
    }

    return controls;
}

QString MediaTools::hashFromName(QString name)
{
    return QString("x") + name.toUtf8().toHex();
}

QString MediaTools::nameFromHash(QString hash)
{
    return QByteArray::fromHex(hash.mid(1).toUtf8());
}

MediaTools::StreamType MediaTools::deviceType(QString device)
{
    QStringList webcams;

    foreach (QVariant deviceName, this->m_webcams)
        webcams << deviceName.toList().at(0).toString();

    QStringList streams;

    foreach (QVariant deviceName, this->m_streams)
        streams << deviceName.toList().at(0).toString();

    if (webcams.contains(device))
        return StreamTypeWebcam;
    else if (streams.contains(device))
        return StreamTypeURI;
    else if (device == "desktop")
        return StreamTypeDesktop;
    else
        return StreamTypeUnknown;
}

void MediaTools::setRecordAudio(bool recordAudio)
{
    this->m_recordAudio = recordAudio;
}

void MediaTools::setRecording(bool recording, QString fileName)
{
    if (!this->m_pipeline)
    {
        this->m_recording = false;
        emit this->recordingChanged(this->m_recording);

        return;
    }

    if (this->m_record->state() != QbElement::ElementStateNull)
    {
        this->m_record->setState(QbElement::ElementStateNull);

        this->m_recording = false;
        emit this->recordingChanged(this->m_recording);
    }

    if (recording)
    {
        QString options = this->bestRecordFormatOptions(fileName);

        if (options == "")
        {
            this->m_recording = false;
            emit this->recordingChanged(this->m_recording);

            return;
        }

        this->m_record->setProperty("location", fileName);
        this->m_record->setProperty("options", options);

/*
        if (error)
        {
            this->m_recording = false;
            emit this->recordingChanged(this->m_recording);

            return;
        }
*/
        this->m_record->setState(QbElement::ElementStatePlaying);
        this->m_recording = true;
        emit this->recordingChanged(this->m_recording);
    }
}

void MediaTools::mutexLock()
{
    this->m_mutex.lock();
}

void MediaTools::mutexUnlock()
{
    this->m_mutex.unlock();
}

void MediaTools::setDevice(QString device)
{
    if (device.isEmpty())
    {
        this->resetRecording();
        this->resetEffectsPreview();

        this->m_source->setState(QbElement::ElementStateNull);

        this->m_device = "";
        emit this->deviceChanged(this->m_device);
    }
    else
    {
        this->m_source->setProperty("location", device);

        this->m_source->setState(QbElement::ElementStatePlaying);
        this->m_device = device;
        emit this->deviceChanged(this->m_device);
    }
}

void MediaTools::setVideoFormat(QVariantList videoFormat, QString device)
{
    QString curDevice = this->device();
    QbElement::ElementState state = this->m_source->state();

    if (state == QbElement::ElementStatePlaying && (device.isEmpty() || device == curDevice))
        this->resetDevice();

    QFile deviceFile(device.isEmpty()? curDevice: device);

    if (!deviceFile.open(QIODevice::ReadWrite | QIODevice::Unbuffered))
        return;

    v4l2_format fmt;
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (ioctl(deviceFile.handle(), VIDIOC_G_FMT, &fmt) == 0)
    {
        fmt.fmt.pix.width = videoFormat.at(0).toUInt();
        fmt.fmt.pix.height = videoFormat.at(1).toUInt();
        fmt.fmt.pix.pixelformat = videoFormat.at(2).toUInt();

        ioctl(deviceFile.handle(), VIDIOC_S_FMT, &fmt);
    }

    deviceFile.close();

    if (state == QbElement::ElementStatePlaying && (device.isEmpty() || device == curDevice))
        this->setDevice(device.isEmpty()? curDevice: device);
}

void MediaTools::setEffectsPreview(bool effectsPreview)
{
    this->m_showEffectsPreview = effectsPreview;
    this->m_effectsPreview->setState(QbElement::ElementStateNull);

    if (effectsPreview && this->m_source->state() != QbElement::ElementStatePlaying)
    {
        QString description = this->m_effectsPreview->property("description").toString();

        if (description.isEmpty())
        {
            description = QString("IN. ! VCapsConvert objectName='preview' "
                                  "caps='video/x-raw,width=%1,height=%2'").arg(128)
                                                                          .arg(96);

            QStringList effects = this->availableEffects().keys();

            foreach (QString effect, effects)
            {
                QString previewHash = this->hashFromName(effect);

                description += QString(", preview. !"
                                       "%1 !"
                                       "QImageConvert objectName='%2'").arg(effect)
                                                                       .arg(previewHash);
            }

            this->m_effectsPreview->setProperty("description", description);

            foreach (QString effect, effects)
            {
                QString previewHash = this->hashFromName(effect);
                QbElementPtr preview;

                QMetaObject::invokeMethod(this->m_effectsPreview.data(),
                                          "element",
                                          Q_RETURN_ARG(QbElementPtr, preview),
                                          Q_ARG(QString, previewHash));

                preview->link(this);
            }
        }

        this->m_effectsPreview->setState(QbElement::ElementStatePlaying);
    }
}

void MediaTools::setVideoRecordFormats(QList<QStringList> videoRecordFormats)
{
    this->m_videoRecordFormats = videoRecordFormats;
}

void MediaTools::setStreams(QVariantList streams)
{
    this->m_streams = streams;
}

void MediaTools::setWindowSize(QSize windowSize)
{
    this->m_windowSize = windowSize;
}

void MediaTools::resetDevice()
{
    this->setDevice("");
}

void MediaTools::resetVideoFormat(QString device)
{
    device = device.isEmpty()? this->device(): device;

    if (!device.isEmpty())
    {
        QVariantList videoFormats = this->videoFormats(device);
        this->setVideoFormat(videoFormats.at(0).toList(), device);
    }
}

void MediaTools::resetEffectsPreview()
{
    this->setEffectsPreview(false);
}

void MediaTools::resetRecordAudio()
{
    this->setRecordAudio(true);
}

void MediaTools::resetRecording()
{
    this->setRecording(false);
}

void MediaTools::resetVideoRecordFormats()
{
    this->setVideoRecordFormats(QList<QStringList>());
}

void MediaTools::resetStreams()
{
    this->setStreams(QVariantList());
}

void MediaTools::resetWindowSize()
{
    this->setWindowSize(QSize());
}

void MediaTools::loadConfigs()
{
    KSharedConfig::Ptr config = KSharedConfig::openConfig(this->m_appEnvironment->configFileName());

    KConfigGroup webcamConfigs = config->group("GeneralConfigs");
    this->enableAudioRecording(webcamConfigs.readEntry("recordAudio", true));
    QStringList windowSize = webcamConfigs.readEntry("windowSize", "320,240").split(",", QString::SkipEmptyParts);
    this->m_windowSize = QSize(windowSize.at(0).trimmed().toInt(),
                               windowSize.at(1).trimmed().toInt());

    KConfigGroup effectsConfigs = config->group("Effects");

    QString effcts = effectsConfigs.readEntry("effects", "");

    if (!effcts.isEmpty())
        this->setEffects(effcts.split("&&", QString::SkipEmptyParts));

    KConfigGroup videoFormatsConfigs = config->group("VideoRecordFormats");

    QString videoRecordFormats = videoFormatsConfigs.
                    readEntry("formats",
                              "webm::"
                              "-r 25 -vcodec libvpx -acodec libvorbis -f webm&&"
                              "ogv, ogg::"
                              "-r 25 -vcodec libtheora -acodec libvorbis -f ogg");

    if (!videoRecordFormats.isEmpty())
        foreach (QString fmt, videoRecordFormats.split("&&", QString::SkipEmptyParts))
        {
            QStringList params = fmt.split("::", QString::SkipEmptyParts);

            this->setVideoRecordFormat(params.at(0),
                                       params.at(1));
        }

    KConfigGroup streamsConfig = config->group("CustomStreams");
    QString streams = streamsConfig.readEntry("streams", "");

    if (!streams.isEmpty())
        foreach (QString fmt, streams.split("&&"))
        {
            QStringList params = fmt.split("::");

            this->setCustomStream(params.at(0).trimmed(),
                                  params.at(1).trimmed(),
                                  params.at(2).trimmed().toInt()? true: false,
                                  params.at(3).trimmed().toInt()? true: false);
        }
}

void MediaTools::saveConfigs()
{
    KSharedConfig::Ptr config = KSharedConfig::openConfig(this->m_appEnvironment->configFileName());

    KConfigGroup webcamConfigs = config->group("GeneralConfigs");

    webcamConfigs.writeEntry("recordAudio", this->m_recordAudio);

    webcamConfigs.writeEntry("windowSize", QString("%1,%2").arg(this->m_windowSize.width())
                                                           .arg(this->m_windowSize.height()));

    KConfigGroup effectsConfigs = config->group("Effects");

    effectsConfigs.writeEntry("effects", this->m_effectsList.join("&&"));

    KConfigGroup videoFormatsConfigs = config->group("VideoRecordFormats");

    QStringList videoRecordFormats;

    foreach (QStringList format, this->m_videoRecordFormats)
        videoRecordFormats << QString("%1::%2").arg(format[0])
                                               .arg(format[1]);

    videoFormatsConfigs.writeEntry("formats",
                                   videoRecordFormats.join("&&"));

    KConfigGroup streamsConfigs = config->group("CustomStreams");

    QStringList streams;

    foreach (QVariant stream, this->m_streams)
        streams << QString("%1::%2::%3::%4").arg(stream.toList().at(0).toString())
                                            .arg(stream.toList().at(1).toString())
                                            .arg(stream.toList().at(2).toInt())
                                            .arg(stream.toList().at(3).toInt());

    streamsConfigs.writeEntry("streams", streams.join("&&"));

    config->sync();
}

void MediaTools::setEffects(QStringList effects)
{
    if (this->m_effectsList == effects)
        return;

    this->m_effectsList = effects;

    if (this->m_effectsList.isEmpty())
        this->m_effects->setProperty("description", "");
    else
    {
        QString description = "IN.";

        foreach (QString effect, this->m_effectsList)
            description += QString(" ! %1").arg(effect);

        description += " ! OUT.";

        this->m_effects->setProperty("description", description);
    }
}

void MediaTools::clearVideoRecordFormats()
{
    this->m_videoRecordFormats.clear();
}

void MediaTools::clearCustomStreams()
{
    this->m_streams.clear();
    emit this->devicesModified();
}

void MediaTools::setCustomStream(QString dev_name, QString description, bool hasAudio, bool playAudio)
{
    this->m_streams << QVariant(QVariantList() << dev_name
                                               << description
                                               << hasAudio
                                               << playAudio
                                               << StreamTypeURI);

    emit this->devicesModified();
}

void MediaTools::enableAudioRecording(bool enable)
{
    this->m_recordAudio = enable;
}

void MediaTools::setVideoRecordFormat(QString suffix, QString options)
{
    this->m_videoRecordFormats << (QStringList() << suffix
                                                 << options);
}

void MediaTools::aboutToQuit()
{
    this->resetDevice();
    this->saveConfigs();
}

void MediaTools::reset(QString device)
{
    this->resetVideoFormat(device);

    QVariantList controls = this->listControls(device);
    QMap<QString, uint> ctrls;

    foreach (QVariant control, controls)
        ctrls[control.toList().at(0).toString()] = control.toList().at(5).toUInt();

    this->setControls(device, ctrls);
}

void MediaTools::onDirectoryChanged(const QString &path)
{
    Q_UNUSED(path)

    emit this->devicesModified();
}