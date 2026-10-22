// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QApplication>
#include <QFont>
#include <fontconfig/fontconfig.h>
#include <mpv/client.h>
static void bdf_mpv_font(mpv_handle *mpv, bool initial) {
    auto family=qApp->property("domeBdfMediaFamily").toString();
    auto file=qApp->property("domeBdfMediaFile").toString();
    int pixels=qApp->property("domeBdfMediaPixels").toInt();
    if(family.isEmpty())family="Unifont";
    if(pixels<=0)pixels=16;
    if(!file.isEmpty())FcConfigAppFontAddFile(nullptr,(const FcChar8 *)file.toUtf8().constData());
    auto setter=initial ? mpv_set_option_string : mpv_set_property_string;
    auto name=family.toUtf8(),size=QByteArray::number(pixels);
    setter(mpv,"osd-font",name.constData());setter(mpv,"sub-font",name.constData());
    setter(mpv,"osd-font-size",size.constData());setter(mpv,"sub-font-size",size.constData());
}
