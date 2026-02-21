/* info_banner_widget.cpp
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <ui/qt/widgets/info_banner_widget.h>

#include <QDate>
#include <QDesktopServices>
#include <QFile>
#include <QFont>
#include <QFontMetrics>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QUrl>
#include <QtMath>

InfoBannerWidget::InfoBannerWidget(QWidget *parent) :
    QFrame(parent)
    , current_slide_(0)
    , auto_advance_timer_(new QTimer(this))
{
    slide_type_visible_[BannerEvents]      = true;
    slide_type_visible_[BannerSponsorship] = true;
    slide_type_visible_[BannerTips]        = true;

    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setFrameShape(QFrame::NoFrame);
    setFixedSize(kCardWidth, kCardHeight);

    loadSlidesFromResource(QStringLiteral(":/json/slides.json"));
    loadSlidesFromResource(QStringLiteral(":/json/slides_custom.json"));
    applySlideFilter();

    connect(auto_advance_timer_, &QTimer::timeout, this, &InfoBannerWidget::advanceSlide);
    auto_advance_timer_->start(kAutoAdvanceMs);
}

// ---------------------------------------------------------------------------
// JSON loading
// ---------------------------------------------------------------------------

void InfoBannerWidget::loadSlidesFromResource(const QString &resource_path)
{
    QFile f(resource_path);
    if (!f.open(QIODevice::ReadOnly))
        return;

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (doc.isNull() || !doc.isObject()) {
        qWarning("InfoBannerWidget: failed to parse %s: %s",
                 qPrintable(resource_path), qPrintable(err.errorString()));
        return;
    }

    QJsonObject root = doc.object();

    int schema = root.value(QStringLiteral("schema_version")).toInt(0);
    if (schema < 1) {
        qWarning("InfoBannerWidget: unsupported schema_version in %s",
                 qPrintable(resource_path));
        return;
    }

    // -----------------------------------------------------------------------
    // Parse optional "general" section
    //
    // Structure:
    //   "general": {
    //       "slide_types": {
    //           "events":      { "color_start": "#rrggbb", "color_end": "#rrggbb" },
    //           "sponsorship": { "color_start": "#rrggbb", "color_end": "#rrggbb" },
    //           "tips":        { "color_start": "#rrggbb", "color_end": "#rrggbb" }
    //       }
    //   }
    // -----------------------------------------------------------------------
    if (root.contains(QStringLiteral("general"))) {
        QJsonObject general = root.value(QStringLiteral("general")).toObject();

        QJsonObject slide_types = general.value(QStringLiteral("slide_types")).toObject();
        for (auto it = slide_types.constBegin(); it != slide_types.constEnd(); ++it) {
            BannerSlideType mapped_type;
            const QString &key = it.key();
            if (key == QLatin1String("events")) {
                mapped_type = BannerEvents;
            } else if (key == QLatin1String("sponsorship")) {
                mapped_type = BannerSponsorship;
            } else if (key == QLatin1String("tips")) {
                mapped_type = BannerTips;
            } else {
                qWarning("InfoBannerWidget: unknown slide type \"%s\" in general section",
                         qPrintable(key));
                continue;
            }

            QJsonObject type_obj = it.value().toObject();
            SlideTypeConfig cfg;
            QString cs = type_obj.value(QStringLiteral("color_start")).toString();
            QString ce = type_obj.value(QStringLiteral("color_end")).toString();
            if (!cs.isEmpty())
                cfg.color_start = QColor(cs);
            if (!ce.isEmpty())
                cfg.color_end = QColor(ce);
            if (cfg.color_start.isValid() || cfg.color_end.isValid())
                slide_type_configs_[mapped_type] = cfg;
        }
    }

    // -----------------------------------------------------------------------
    // Parse "slides" array
    // -----------------------------------------------------------------------
    QJsonArray slides_arr = root.value(QStringLiteral("slides")).toArray();
    for (const QJsonValue &val : slides_arr) {
        if (!val.isObject())
            continue;
        QJsonObject obj = val.toObject();

        QString type_str = obj.value(QStringLiteral("type")).toString();
        BannerSlideType slide_type;
        if (type_str == QLatin1String("events")) {
            slide_type = BannerEvents;
        } else if (type_str == QLatin1String("sponsorship")) {
            slide_type = BannerSponsorship;
        } else if (type_str == QLatin1String("tips")) {
            slide_type = BannerTips;
        } else {
            qWarning("InfoBannerWidget: unknown slide type \"%s\", skipping",
                     qPrintable(type_str));
            continue;
        }

        BannerSlide slide;
        slide.type            = slide_type;
        slide.tag             = obj.value(QStringLiteral("tag")).toString();
        slide.title           = obj.value(QStringLiteral("title")).toString();
        slide.description     = obj.value(QStringLiteral("description")).toString();
        slide.description_sub = obj.value(QStringLiteral("description_sub")).toString();
        slide.body_text       = obj.value(QStringLiteral("body_text")).toString();
        slide.button_label    = obj.value(QStringLiteral("button_label")).toString();
        slide.url             = obj.value(QStringLiteral("url")).toString();
        slide.image           = obj.value(QStringLiteral("image")).toString();

        QString df = obj.value(QStringLiteral("date_from")).toString();
        QString du = obj.value(QStringLiteral("date_until")).toString();
        if (!df.isEmpty())
            slide.date_from  = QDate::fromString(df, Qt::ISODate);
        if (!du.isEmpty())
            slide.date_until = QDate::fromString(du, Qt::ISODate);

        all_slides_.append(slide);
    }
}

// ---------------------------------------------------------------------------
// Filtering
// ---------------------------------------------------------------------------

void InfoBannerWidget::applySlideFilter()
{
    QDate today = QDate::currentDate();
    slides_.clear();

    for (const BannerSlide &slide : all_slides_) {
        // Date range check
        if (slide.date_from.isValid()  && today < slide.date_from)  continue;
        if (slide.date_until.isValid() && today > slide.date_until) continue;

        // Per-type visibility (set via preferences)
        if (!slide_type_visible_.value(slide.type, true)) continue;

        slides_.append(slide);
    }

    current_slide_ = 0;
    update();
}

void InfoBannerWidget::setSlideTypeVisible(BannerSlideType type, bool visible)
{
    slide_type_visible_[type] = visible;
    applySlideFilter();
}

// ---------------------------------------------------------------------------
// Gradient colors
//
// Uses colors from the "general.slide_types" JSON section when available;
// falls back to built-in defaults otherwise.
// ---------------------------------------------------------------------------

QPair<QColor, QColor> InfoBannerWidget::gradientForType(BannerSlideType type) const
{
    if (slide_type_configs_.contains(type)) {
        const SlideTypeConfig &cfg = slide_type_configs_[type];
        QColor cs = cfg.color_start.isValid() ? cfg.color_start : QColor(0x33, 0x33, 0x33);
        QColor ce = cfg.color_end.isValid()   ? cfg.color_end   : QColor(0x22, 0x22, 0x22);
        return { cs, ce };
    }

    // Built-in defaults (matches the values originally defined in slides.json
    // "general.slide_types" so behaviour is identical when the section is absent)
    switch (type) {
    case BannerEvents:
        return { QColor(0x1a, 0x4a, 0x6e), QColor(0x23, 0x4d, 0x6e) };
    case BannerSponsorship:
        return { QColor(0x2d, 0x4a, 0x3e), QColor(0x1e, 0x3a, 0x32) };
    case BannerTips:
        return { QColor(0x4a, 0x3d, 0x6e), QColor(0x3a, 0x2d, 0x5e) };
    }
    return { QColor(0x33, 0x33, 0x33), QColor(0x22, 0x22, 0x22) };
}

// ---------------------------------------------------------------------------
// Auto-advance
// ---------------------------------------------------------------------------

void InfoBannerWidget::advanceSlide()
{
    if (slides_.isEmpty()) return;
    current_slide_ = (current_slide_ + 1) % slides_.size();
    update();
}

void InfoBannerWidget::updateStyleSheets()
{
    update();
}

// ---------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------

QRect InfoBannerWidget::dotRect(int index) const
{
    const QRect r = rect();
    int num_slides = slides_.size();
    int total_w = num_slides * (kDotRadius * 2) + (num_slides - 1) * (kDotSpacing - kDotRadius * 2);
    int dots_x  = (r.width()  - total_w)   / 2;
    int dots_y  =  r.height() - kDotBottomMargin;
    int margin  = 6;
    return QRect(
        dots_x + index * kDotSpacing - margin,
        dots_y - kDotRadius - margin,
        kDotRadius * 2 + margin * 2,
        kDotRadius * 2 + margin * 2
    );
}

int InfoBannerWidget::dotHitTest(const QPoint &pos) const
{
    for (int i = 0; i < slides_.size(); ++i) {
        if (dotRect(i).contains(pos))
            return i;
    }
    return -1;
}

QRect InfoBannerWidget::buttonRect() const
{
    const QRect r = rect();
    int btn_h = 32;
    int btn_w = r.width() - kContentLeftMargin - kContentRightMargin;
    int btn_y = r.height() - kDotBottomMargin - kDotRadius * 2 - 12 - btn_h;
    return QRect(kContentLeftMargin, btn_y, btn_w, btn_h);
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

void InfoBannerWidget::paintEvent(QPaintEvent * /* event */)
{
    if (slides_.isEmpty()) return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const BannerSlide &slide = slides_[current_slide_];
    const QRectF r = rect();

    // --- Background gradient (145 degrees) ---
    double angle_rad = qDegreesToRadians(145.0);
    double cx = r.width()  / 2.0;
    double cy = r.height() / 2.0;
    double dx = qCos(angle_rad - M_PI_2) * r.width();
    double dy = qSin(angle_rad - M_PI_2) * r.height();

    QPair<QColor, QColor> colors = gradientForType(slide.type);
    QLinearGradient gradient(
        QPointF(cx - dx / 2.0, cy - dy / 2.0),
        QPointF(cx + dx / 2.0, cy + dy / 2.0)
    );
    gradient.setColorAt(0, colors.first);
    gradient.setColorAt(1, colors.second);

    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawRoundedRect(r, 8, 8);

    // --- Illustration area ---
    QRectF illus_rect(0, 0, r.width(), kIllustrationHeight);
    {
        QPainterPath clip;
        clip.moveTo(0, 8);
        clip.quadTo(0, 0, 8, 0);
        clip.lineTo(r.width() - 8, 0);
        clip.quadTo(r.width(), 0, r.width(), 8);
        clip.lineTo(r.width(), kIllustrationHeight);
        clip.lineTo(0, kIllustrationHeight);
        clip.closeSubpath();

        if (!slide.image.isEmpty()) {
            QString img_path = QStringLiteral(":/json/banners/") + slide.image;
            QPixmap px(img_path);
            if (!px.isNull()) {
                painter.save();
                painter.setClipPath(clip);
                qreal dpr = devicePixelRatioF();
                px.setDevicePixelRatio(dpr);
                painter.drawPixmap(illus_rect.toRect(),
                                   px.scaled(illus_rect.width()  * dpr,
                                             illus_rect.height() * dpr,
                                             Qt::KeepAspectRatioByExpanding,
                                             Qt::SmoothTransformation));
                painter.restore();
            }
        }

        // Semi-transparent overlay on illustration
        painter.save();
        painter.setClipPath(clip);
        painter.setBrush(QColor(0, 0, 0, 60));
        painter.setPen(Qt::NoPen);
        painter.drawRect(illus_rect);
        painter.restore();
    }

    // --- Tag pill ---
    QFont tag_font = font();
    tag_font.setPixelSize(9);
    tag_font.setBold(true);
    tag_font.setCapitalization(QFont::AllUppercase);
    painter.setFont(tag_font);

    QString tag_text = slide.tag.toUpper();
    QFontMetrics tag_fm(tag_font);
    int pill_h = tag_fm.height() + 6;
    int pill_w = tag_fm.horizontalAdvance(tag_text) + 16;
    QRectF pill_rect(kContentLeftMargin,
                     kIllustrationHeight + 12,
                     pill_w, pill_h);

    painter.setBrush(QColor(0, 0, 0, 60));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(pill_rect, pill_h / 2.0, pill_h / 2.0);

    painter.setPen(QColor(255, 255, 255, 220));
    painter.drawText(pill_rect, Qt::AlignCenter, tag_text);

    // --- Title ---
    QFont title_font = font();
    title_font.setPixelSize(15);
    title_font.setBold(true);
    painter.setFont(title_font);
    painter.setPen(Qt::white);

    int content_w = r.width() - kContentLeftMargin - kContentRightMargin;
    int text_y = kIllustrationHeight + 12 + pill_h + 10;
    painter.drawText(QRectF(kContentLeftMargin, text_y, content_w, 22),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     slide.title);

    // --- Description highlight box ---
    text_y += 26;
    int box_h = 46;
    QRectF box_rect(kContentLeftMargin, text_y, content_w, box_h);
    painter.setBrush(QColor(255, 255, 255, 30));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(box_rect, 6, 6);

    QFont desc_font = font();
    desc_font.setPixelSize(12);
    painter.setFont(desc_font);
    painter.setPen(Qt::white);
    painter.drawText(QRectF(kContentLeftMargin + 8, text_y + 4, content_w - 16, 18),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     slide.description);

    QFont desc_sub_font = font();
    desc_sub_font.setPixelSize(11);
    painter.setFont(desc_sub_font);
    painter.setPen(QColor(255, 255, 255, 200));
    painter.drawText(QRectF(kContentLeftMargin + 8, text_y + 24, content_w - 16, 16),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     slide.description_sub);

    // --- Action button ---
    if (!slide.button_label.isEmpty()) {
        QRect btn = buttonRect();
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(255, 255, 255, 180), 1));
        painter.drawRoundedRect(btn, 4, 4);

        QFont btn_font = font();
        btn_font.setPixelSize(11);
        btn_font.setBold(true);
        painter.setFont(btn_font);
        painter.setPen(Qt::white);
        painter.drawText(btn, Qt::AlignCenter, slide.button_label);
    }

    // --- Navigation dots ---
    int num_slides = slides_.size();
    int total_w = num_slides * (kDotRadius * 2) + (num_slides - 1) * (kDotSpacing - kDotRadius * 2);
    int dots_x  = (r.width() - total_w) / 2;
    int dots_y  = r.height() - kDotBottomMargin;

    painter.setPen(Qt::NoPen);
    for (int i = 0; i < num_slides; ++i) {
        QRectF dot(dots_x + i * kDotSpacing,
                   dots_y - kDotRadius,
                   kDotRadius * 2, kDotRadius * 2);
        painter.setBrush(i == current_slide_
                         ? QColor(255, 255, 255, 230)
                         : QColor(255, 255, 255, 100));
        painter.drawEllipse(dot);
    }
}

// ---------------------------------------------------------------------------
// Mouse handling
// ---------------------------------------------------------------------------

void InfoBannerWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QFrame::mousePressEvent(event);
        return;
    }

    int dot_index = dotHitTest(event->pos());
    if (dot_index >= 0 && dot_index != current_slide_) {
        current_slide_ = dot_index;
        auto_advance_timer_->start(kAutoAdvanceMs);
        update();
        return;
    }

    if (current_slide_ >= 0 && current_slide_ < slides_.size()) {
        const QString &url = slides_[current_slide_].url;
        if (!url.isEmpty())
            QDesktopServices::openUrl(QUrl(url));
    }
}

void InfoBannerWidget::mouseMoveEvent(QMouseEvent *event)
{
    Q_UNUSED(event)
    setCursor(Qt::PointingHandCursor);
    QFrame::mouseMoveEvent(event);
}
