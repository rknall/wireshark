/** @file
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef INFO_BANNER_WIDGET_H
#define INFO_BANNER_WIDGET_H

#include <QDate>
#include <QFrame>
#include <QColor>
#include <QString>
#include <QList>
#include <QMap>
#include <QPair>
#include <QTimer>

enum BannerSlideType {
    BannerEvents,
    BannerSponsorship,
    BannerTips,
};

/**
 * Per-type visual configuration loaded from the "general.slide_types"
 * section of slides.json.  Colors default to the built-in palette when
 * the section is absent or a type is not listed.
 */
struct SlideTypeConfig {
    QColor color_start;
    QColor color_end;
};

struct BannerSlide {
    BannerSlideType type;
    QString tag;            // short label shown as pill/badge
    QString title;
    QString description;    // primary text in the highlight box
    QString description_sub; // secondary line in the highlight box
    QString body_text;      // additional paragraph below highlight box
    QString button_label;   // action button text
    QString url;            // click target
    QString image;          // banner image filename under :/json/banners/
    QDate date_from;        // slide hidden before this date (invalid = always visible)
    QDate date_until;       // slide hidden after this date  (invalid = always visible)
};

class InfoBannerWidget : public QFrame {
    Q_OBJECT
public:
    explicit InfoBannerWidget(QWidget *parent = nullptr);

    void updateStyleSheets();
    void setSlideTypeVisible(BannerSlideType type, bool visible);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    QList<BannerSlide> all_slides_;   // unfiltered master list
    QList<BannerSlide> slides_;       // currently visible slides
    int current_slide_;
    QTimer *auto_advance_timer_;

    QMap<BannerSlideType, bool>           slide_type_visible_;
    QMap<BannerSlideType, SlideTypeConfig> slide_type_configs_;

    void loadSlidesFromResource(const QString &resource_path);
    void applySlideFilter();
    void advanceSlide();
    QPair<QColor, QColor> gradientForType(BannerSlideType type) const;
    int  dotHitTest(const QPoint &pos) const;
    QRect dotRect(int index) const;
    QRect buttonRect() const;

    // Layout constants
    static constexpr int kCardWidth          = 300;
    static constexpr int kCardHeight         = 360;
    static constexpr int kIllustrationHeight = 120;
    static constexpr int kDotRadius          = 4;
    static constexpr int kDotSpacing         = 12;
    static constexpr int kDotBottomMargin    = 16;
    static constexpr int kContentLeftMargin  = 16;
    static constexpr int kContentRightMargin = 16;
    static constexpr int kAutoAdvanceMs      = 10000;
};

#endif // INFO_BANNER_WIDGET_H
