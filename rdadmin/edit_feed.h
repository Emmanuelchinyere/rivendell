// edit_feed.h
//
// Edit a Rivendell Feed
//
//   (C) Copyright 2002-2020 Fred Gleason <fredg@paravelsystems.com>
//
//   This program is free software; you can redistribute it and/or modify
//   it under the terms of the GNU General Public License version 2 as
//   published by the Free Software Foundation.
//
//   This program is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//   GNU General Public License for more details.
//
//   You should have received a copy of the GNU General Public
//   License along with this program; if not, write to the Free Software
//   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//

#ifndef EDIT_FEED_H
#define EDIT_FEED_H

#include <qcheckbox.h>
#include <qcombobox.h>
#include <qgroupbox.h>
#include <qlabel.h>
#include <qlineedit.h>
#include <qpushbutton.h>
#include <qspinbox.h>
#include <qtextedit.h>

#include <rddialog.h>
#include <rdfeed.h>
#include <rdsettings.h>
#include <rdstation.h>

class EditFeed : public RDDialog
{
 Q_OBJECT
 public:
  EditFeed(const QString &feed,QWidget *parent=0);
  QSize sizeHint() const;
  QSizePolicy sizePolicy() const;

 private slots:
   //  void isSuperfeedChangedData(int n);
  void comboboxActivatedData(int n);
  void checkboxToggledData(bool state);
  void lineeditChangedData(const QString &str);
  void selectSubfeedsData();
  void setFormatData();
  void redirectToggledData(bool state);
  void okData();
  void cancelData();

 protected:
  void resizeEvent(QResizeEvent *e);
  
 private:
  void UpdateControlState();
  RDFeed *feed_feed;
  QLabel *feed_keyname_label;
  QLineEdit *feed_keyname_edit;
  QLabel *feed_is_superfeed_label;
  QPushButton *feed_is_superfeed_button;
  QCheckBox *feed_audience_metrics_check;
  QLabel *feed_audience_metrics_label;
  QComboBox *feed_is_superfeed_box;
  QLineEdit *feed_channel_title_edit;
  QTextEdit *feed_channel_description_edit;
  QLineEdit *feed_channel_category_edit;
  QLineEdit *feed_channel_link_edit;
  QLineEdit *feed_channel_copyright_edit;
  QLabel *feed_channel_editor_label;
  QLineEdit *feed_channel_editor_edit;
  QLabel *feed_channel_webmaster_label;
  QLineEdit *feed_channel_webmaster_edit;
  QLineEdit *feed_channel_language_edit;
  QLineEdit *feed_base_url_edit;
  QLineEdit *feed_base_preamble_edit;
  QLineEdit *feed_purge_url_edit;
  QLabel *feed_purge_username_label;
  QLineEdit *feed_purge_username_edit;
  QLabel *feed_purge_password_label;
  QLineEdit *feed_purge_password_edit;
  QLabel *feed_rss_schema_label;
  QComboBox *feed_rss_schema_box;
  QTextEdit *feed_header_xml_edit;
  QTextEdit *feed_channel_xml_edit;
  QTextEdit *feed_item_xml_edit;
  QSpinBox *feed_max_shelf_life_spin;
  QCheckBox *feed_autopost_box;
  QCheckBox *feed_keep_metadata_box;
  RDSettings feed_settings;
  QLineEdit *feed_format_edit;
  QCheckBox *feed_normalize_check;
  QLabel *feed_normalize_label;
  QSpinBox *feed_normalize_spin;
  QLineEdit *feed_extension_edit;
  QComboBox *feed_castorder_box;
  QComboBox *feed_media_link_mode_box;
  QLabel *feed_redirect_label;
  QCheckBox *feed_redirect_check;
  QLabel *feed_redirect_url_label;
  QLineEdit *feed_redirect_url_edit;
  QPushButton *feed_format_button;
  QGroupBox *feed_channel_section_groupbox;
  QLabel *feed_channel_title_label;
  QLabel *feed_channel_category_label;
  QLabel *feed_channel_link_label;
  QLabel *feed_channel_copyright_label;
  QLabel *feed_channel_language_label;
  QLabel *feed_channel_description_label;
  QLabel *feed_base_url_label;
  QLabel *feed_base_preamble_label;
  QLabel *feed_purge_url_label;
  QLabel *feed_max_shelf_life_label;
  QLabel *feed_max_shelf_life_unit_label;
  QLabel *feed_autopost_label;
  QLabel *feed_keep_metadata_label;
  QLabel *feed_format_label;
  QLabel *feed_normalize_check_label;
  QLabel *feed_normalize_unit_label;
  QLabel *feed_castorder_label;
  QLabel *feed_media_link_mode_label;
  QLabel *feed_extension_label;
  QPushButton *feed_ok_button;
  QPushButton *feed_cancel_button;
  QLabel *feed_header_xml_label;
  QLabel *feed_channel_xml_label;
  QLabel *feed_item_xml_label;
};


#endif  // EDIT_FEED_H

