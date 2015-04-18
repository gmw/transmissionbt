/*
 * This file Copyright (C) 2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <QEvent>
#include <QGridLayout>
#include <QTimer>

#include "column-resizer.h"

namespace
{
  int
  itemColumnSpan (const QGridLayout * layout, const QLayoutItem * item)
  {
    for (int i = 0, count = layout->count (); i < count; ++i)
      {
        if (layout->itemAt (i) != item)
          continue;

        int row, column, rowSpan, columnSpan;
        layout->getItemPosition (i, &row, &column, &rowSpan, &columnSpan);
        return columnSpan;
      }

    return 0;
  }
}

ColumnResizer::ColumnResizer (QObject * parent):
  QObject (parent),
  myTimer (new QTimer (this)),
  myLayouts ()
{
  myTimer->setSingleShot (true);
  connect (myTimer, SIGNAL (timeout ()), SLOT (update ()));
}

void
ColumnResizer::addLayout (QGridLayout * layout)
{
  myLayouts << layout;
  scheduleUpdate ();
}

bool
ColumnResizer::eventFilter (QObject * object, QEvent * event)
{
  if (event->type () == QEvent::Resize)
    scheduleUpdate ();
  return QObject::eventFilter (object, event);
}

void
ColumnResizer::update ()
{
  int maxWidth = 0;
  for (const QGridLayout * const layout: myLayouts)
    {
      for (int i = 0, count = layout->rowCount (); i < count; ++i)
        {
          QLayoutItem * item = layout->itemAtPosition (i, 0);
          if (item == nullptr || itemColumnSpan (layout, item) > 1)
            continue;
          maxWidth = qMax (maxWidth, item->sizeHint ().width ());
        }
    }

  for (QGridLayout * const layout: myLayouts)
    layout->setColumnMinimumWidth (0, maxWidth);
}

void
ColumnResizer::scheduleUpdate ()
{
  myTimer->start (0);
}
