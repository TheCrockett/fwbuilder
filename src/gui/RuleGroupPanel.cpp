/*

                          Firewall Builder

                 Copyright (C) 2003 NetCitadel, LLC

  Author:  Vadim Kurland     vadim@fwbuilder.org

  $Id: RuleGroupPanel.cpp,v 1.163 2007/05/30 04:24:55 vkurland Exp $

  This program is free software which we release under the GNU General Public
  License. You may redistribute and/or modify this program under the terms
  of that license as published by the Free Software Foundation; either
  version 2 of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  To get a copy of the GNU General Public License, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#include "RuleGroupPanel.h"
#include "RuleSetView.h"
RuleGroupPanel::RuleGroupPanel (QWidget * parent,RuleSetView * rsv, int row) : QFrame(parent)
{
    this->row= row;
    this->rsv = rsv;
    this->setupUi(this);
    setContentsMargins (3,3,3,3);
    showHideRuleGroupButton->hide();
    connect (showHideRuleGroupButton, SIGNAL(pressed()),this,SLOT(showHideRuleGroup()));
}

void RuleGroupPanel::mousePressEvent ( QMouseEvent * event )
{
    if (event->buttons () == Qt::RightButton)
    {
        rsv->firstSelectedRule = row;
        rsv->contextMenu (row,0,event->globalPos ());
    }
}

void RuleGroupPanel::mouseDoubleClickEvent ( QMouseEvent * event )
{
    rsv->firstSelectedRule = row ;
    rsv->renameGroup();
}

void RuleGroupPanel::showHideRuleGroup()
{
    rsv->showHideRuleGroup(this);
}
