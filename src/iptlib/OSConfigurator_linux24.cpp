/* 

                          Firewall Builder

                 Copyright (C) 2002 NetCitadel, LLC

  Author:  Vadim Kurland     vadim@vk.crocodile.org

  $Id$

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


#include "config.h"

#include "OSConfigurator_linux24.h"

#include "fwbuilder/InetAddr.h"

#include "fwbuilder/Firewall.h"
#include "fwbuilder/FWOptions.h"
#include "fwbuilder/Interface.h"
#include "fwbuilder/IPv4.h"
#include "fwbuilder/Network.h"
#include "fwbuilder/Address.h"
#include "fwbuilder/Resources.h"
#include "fwbuilder/MultiAddress.h"
#include "fwbuilder/FailoverClusterGroup.h"

#include "Configlet.h"

#ifndef _WIN32
#  include <unistd.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>

#include <algorithm>
#include <functional>
#include <iostream>

#include <assert.h>

#include <QString>

using namespace libfwbuilder;
using namespace fwcompiler;
using namespace std;

string OSConfigurator_linux24::myPlatformName() { return "Linux24"; }

OSConfigurator_linux24::OSConfigurator_linux24(FWObjectDatabase *_db,
                                               Firewall *fw,
                                               bool ipv6_policy) : 
    OSConfigurator(_db, fw, ipv6_policy) , os_data(fw->getStr("host_OS"))
{
}

string OSConfigurator_linux24::getInterfaceVarName(FWObject *iface, bool v6)
{
    ostringstream  ostr;
    string iname=iface->getName();
    string::size_type p1;
    while ( (p1=iname.find("."))!=string::npos)
        iname=iname.replace(p1,1,"_");
    ostr << "i_" << iname;
    if (v6) ostr << "_v6";
    return ostr.str();
}

void OSConfigurator_linux24::setConfigletMacroForOptionStr(
    FWOptions *opt, Configlet *c, const char *option_name)
{
    const string s = opt->getStr(option_name);
    // "" means no change, do not include the command in the script
    if (!s.empty())
    {
        c->setVariable(QString("if_") + option_name, 1);
        c->setVariable(option_name, s.c_str());
    } else
    {
        c->setVariable(QString("if_") + option_name, 0);
        c->setVariable(option_name, s.c_str());
    }
}

void OSConfigurator_linux24::setConfigletMacroForOptionInt(
    FWOptions *opt, Configlet *c, const char *option_name)
{
    int val = opt->getInt(option_name);
    // -1 means no change, do not include the command in the script
    if (val >= 0)
    {
        c->setVariable(QString("if_") + option_name, 1);
        c->setVariable(option_name, val);
    } else
    {
        c->setVariable(QString("if_") + option_name, 0);
        c->setVariable(option_name, 0);
    }
}

void OSConfigurator_linux24::processFirewallOptions() 
{
    Configlet kernel_vars(fw, "linux24", "kernel_vars");
    kernel_vars.removeComments();
    kernel_vars.collapseEmptyStrings(true);

    FWOptions* options = fw->getOptionsObject();

    setConfigletMacroForOptionStr(options, &kernel_vars, "linux24_ip_dynaddr");
    setConfigletMacroForOptionStr(options, &kernel_vars, "linux24_rp_filter");
    setConfigletMacroForOptionStr(options, &kernel_vars, "linux24_accept_source_route");
    setConfigletMacroForOptionStr(options, &kernel_vars, "linux24_accept_redirects");
    setConfigletMacroForOptionStr(options, &kernel_vars, "linux24_log_martians");
    setConfigletMacroForOptionStr(options, &kernel_vars, "linux24_icmp_echo_ignore_broadcasts");
    setConfigletMacroForOptionStr(options, &kernel_vars, "linux24_icmp_echo_ignore_all");
    setConfigletMacroForOptionStr(options, &kernel_vars, "linux24_icmp_ignore_bogus_error_responses");
    setConfigletMacroForOptionStr(options, &kernel_vars, "linux24_tcp_window_scaling");
    setConfigletMacroForOptionStr(options, &kernel_vars, "linux24_tcp_sack");
    setConfigletMacroForOptionStr(options, &kernel_vars, "linux24_tcp_fack");
    setConfigletMacroForOptionStr(options, &kernel_vars, "linux24_tcp_syncookies");
    setConfigletMacroForOptionStr(options, &kernel_vars, "linux24_tcp_ecn");
    setConfigletMacroForOptionStr(options, &kernel_vars, "linux24_tcp_timestamps");

    setConfigletMacroForOptionInt(options, &kernel_vars, "linux24_tcp_fin_timeout");
    setConfigletMacroForOptionInt(options, &kernel_vars, "linux24_tcp_keepalive_interval");

    output << kernel_vars.expand().toStdString();
}

void OSConfigurator_linux24::addVirtualAddressForNAT(const Network *nw)
{
    FWOptions* options=fw->getOptionsObject();
    if (options->getBool("manage_virtual_addr"))
    {
        if (virtual_addresses.empty() || 
            find(virtual_addresses.begin(),virtual_addresses.end(),
                 *(nw->getAddressPtr())) == virtual_addresses.end())
        {
            Interface *iface = findInterfaceFor( nw, fw );
            if (iface!=NULL)
            {
                const InetAddr *addr = nw->getAddressPtr();
                InetAddr first, last;
                InetAddr a;

                first = *addr;
                last = *(nw->getBroadcastAddressPtr());

                QStringList addresses;
                for (a=first; a<last; a=a+1)
                {
                    addresses.push_back(QString("%1/32").arg(a.toString().c_str()));
                }

                if (virtual_addresses_for_nat.count(iface->getName()) > 0)
                    addresses.push_front(virtual_addresses_for_nat[iface->getName()].c_str());

                virtual_addresses_for_nat[iface->getName()] =
                    addresses.join(" ").toStdString();

                virtual_addresses.push_back( *(nw->getAddressPtr()) );
                registerVirtualAddressForNat();
            } else
                warning("Can not add virtual address " +
                        nw->getAddressPtr()->toString() +
                        " (object " + nw->getName() + ")" );
        }
    }
}

void OSConfigurator_linux24::addVirtualAddressForNAT(const Address *addr)
{
    FWOptions* options=fw->getOptionsObject();
    if ( options->getBool("manage_virtual_addr") ) 
    {
        if (virtual_addresses.empty() || 
            find(virtual_addresses.begin(),virtual_addresses.end(),
                 *(addr->getAddressPtr())) == virtual_addresses.end()) 
        {
            FWObject *vaddr = findAddressFor(addr, fw );
            if (vaddr!=NULL)
            {
                Interface *iface = Interface::cast(vaddr->getParent());
                assert(iface!=NULL);

                QStringList addresses;
                const InetAddr *vaddr_netm =
                    Address::cast(vaddr)->getNetmaskPtr();

                addresses.push_back(QString("%1/%2").
                                    arg(addr->getAddressPtr()->toString().c_str()).
                                    arg(vaddr_netm->getLength()));

                if (virtual_addresses_for_nat.count(iface->getName()) > 0)
                    addresses.push_front(virtual_addresses_for_nat[iface->getName()].c_str());

                virtual_addresses_for_nat[iface->getName()] =
                    addresses.join(" ").toStdString();
        
                virtual_addresses.push_back(*(addr->getAddressPtr()));
                registerVirtualAddressForNat();
            } else
                warning("Can not add virtual address " + 
                        addr->getAddressPtr()->toString() +
                        " (object " + addr->getName() + ")" );
        }
        return;
    }
}

void OSConfigurator_linux24::registerMultiAddressObject(MultiAddressRunTime *at)
{
    address_table_objects[at->getName()] = at->getSourceName();
}

void OSConfigurator_linux24::printChecksForRunTimeMultiAddress()
{
    output << "# Using " << address_table_objects.size() << " address table files" << endl;

    map<string,string>::iterator i;
    for (i=address_table_objects.begin(); i!=address_table_objects.end(); ++i)
    {
        string at_name = i->first;
        string at_file = i->second;
        output << "check_file \"" + at_name + "\" \"" + at_file + "\"" << endl;
    }
}

int  OSConfigurator_linux24::prolog()
{
    return 0;
}

/**
 * Print shell functions used by the script. If argument (boolean) is true,
 * do not add comments.
 */
string OSConfigurator_linux24::printShellFunctions()
{
    ostringstream str;
    FWOptions* options = fw->getOptionsObject();

    // string host_os = fw->getStr("host_OS");
    // string os_family = Resources::os_res[host_os]->
    //     getResourceStr("/FWBuilderResources/Target/family");

    Configlet shell_functions(fw, "linux24", "shell_functions");
    str << shell_functions.expand().toStdString();

/* check if package iproute2 is installed, but do this only if
 * we really need /usr/sbin/ip 
 */
    if (options->getBool("verify_interfaces") || 
        options->getBool("manage_virtual_addr") ||
        options->getBool("configure_interfaces") )
    {
        Configlet configlet(fw, "linux24", "check_utilities");
        configlet.removeComments();

        configlet.setVariable("need_vconfig", 
                              options->getBool("configure_vlan_interfaces"));
        configlet.setVariable("need_brctl", 
                              options->getBool("configure_bridge_interfaces"));
        configlet.setVariable("need_ifenslave", 
                              options->getBool("configure_bonding_interfaces"));
        
        str << configlet.expand().toStdString();
    }

    /*
     * Generate commands to reset all tables and chains and set
     * default policy
     */
    Configlet reset_iptables(fw, "linux24", "reset_iptables");
    str << reset_iptables.expand().toStdString();

    Configlet addr_conf(fw, "linux24", "update_addresses");
    str << addr_conf.expand().toStdString();
    str << "\n";

    if (options->getBool("configure_vlan_interfaces"))
    {
        Configlet conf(fw, "linux24", "update_vlans");
        str << conf.expand().toStdString();
        str << "\n";
    }

    if (options->getBool("configure_bridge_interfaces"))
    {
        Configlet conf(fw, "linux24", "update_bridge");
        str << conf.expand().toStdString();
        str << "\n";
    }

    if (options->getBool("configure_bonding_interfaces"))
    {
        Configlet conf(fw, "linux24", "update_bonding");
        str << conf.expand().toStdString();
        str << "\n";
    }

    return str.str();
}

string OSConfigurator_linux24::getPathForATool(const std::string &os_variant, OSData::tools tool_name)
{
    FWOptions* options = fw->getOptionsObject();
    string attr = os_data.getAttributeNameForTool(tool_name);

    string s = options->getStr("linux24_" + attr);
    if (!s.empty()) return s;

    string host_os = fw->getStr("host_OS");
    string r = "/FWBuilderResources/Target/tools/" + os_variant + "/" + attr;
    if (Resources::os_res[host_os]->getResourceStr(r).empty())
        r = "/FWBuilderResources/Target/tools/Unknown/" + attr;
    
    return Resources::os_res[host_os]->getResourceStr(r);
}

string  OSConfigurator_linux24::printPathForAllTools(const string &os)
{
    ostringstream res;

    list<int>::const_iterator i;
    const list<int> &all_tools = os_data.getAllTools(); 
    for (i=all_tools.begin(); i!=all_tools.end(); ++i)
        res << os_data.getVariableName(OSData::tools(*i))
            << "=\""
            << getPathForATool(os, OSData::tools(*i))
            << "\""
            << endl;
    return res.str();
}

string OSConfigurator_linux24::generateCodeForProtocolHandlers()
{
    ostringstream ostr;
    FWOptions* options = fw->getOptionsObject();
    bool nomod = Resources::os_res[fw->getStr("host_OS")]->
        Resources::getResourceBool("/FWBuilderResources/Target/options/suppress_modules");

    // string host_os = fw->getStr("host_OS");
    // string os_family = Resources::os_res[host_os]->
    //     getResourceStr("/FWBuilderResources/Target/family");
    Configlet load_modules(fw, "linux24", "load_modules");
    load_modules.removeComments();

    // See ticket #2
    string modules_dir = Resources::os_res[fw->getStr("host_OS")]->
        Resources::getResourceStr("/FWBuilderResources/Target/options/default/modules_dir");

/* there is no need to load modules on some platforms */
    load_modules.setVariable("load_modules", options->getBool("load_modules") && !nomod);
    load_modules.setVariable("modules_dir", modules_dir.c_str());
    ostr << load_modules.expand().toStdString();

    return ostr.str();
}

string  OSConfigurator_linux24::printRunTimeWrappers(FWObject *rule,
                                                     const string &command,
                                                     bool ipv6)
{
    string command_line = command;
    ostringstream  ext_command_line;

    int nlines = 0;
    string::size_type p1 = 0;
    string::size_type p2, p3;

    p1=command_line.find("$at_");
    if ( p1!=string::npos )
    {
        p2=command_line.find(" ",p1);
        string at_var= command_line.substr(p1+1,p2-p1-1);  // skip '$'
        string atfile = rule->getStr("address_table_file");
        ext_command_line << "grep -Ev '^#|^;|^\\s*$' " << atfile << " | ";
        ext_command_line << "while read L ; do" << endl;
        ext_command_line << "  set $L; " << at_var << "=$1; ";
        ext_command_line << command_line;
        ext_command_line << "done" << endl;

        command_line = ext_command_line.str();
    }

    p1 = 0;
    while (1)
    {
        p1=command_line.find_first_of("\n\r",p1);
        if (p1==string::npos) break;
        nlines++;
        p1=command_line.find_first_not_of("\n\r",p1);
        if (p1==string::npos) break;
    }

/* if anywhere in command_line we used variable holding an address of
 * dynamic interface (named $i_something) then we need to add this
 * command with a check for the value of this variable. We execute
 * iptables command only if the value is a non-empty string.
 *
 * bug #1851166: there could be two dynamic interfaces in the same
 * rule.
 */
    if (command_line.find("$i_")==string::npos) return command_line;

    string getaddr_function_name = "getaddr";
    if (ipv6) getaddr_function_name = "getaddr6";

    ostringstream  res;
    bool wildcard_interfaces = false;
    p1=0;
    while ((p1=command_line.find("$i_", p1))!=string::npos)
    {
        string  iface_name;
        string  iface_var;

        p2=command_line.find(" ",p1);
        p3=command_line.find("_",p1) +1;
        iface_name=command_line.substr(p3,p2-p3);
        iface_var= command_line.substr(p1,p2-p1);

/* if interface name ends with '*', this is a wildcard interface. */
        string::size_type p4;
        if ((p4=iface_name.find("*"))!=string::npos)
        {
            wildcard_interfaces = true;
            string cmdline=command_line;
            string iface_family_name=iface_name.substr(0,p4);
            res << "getinterfaces " << iface_family_name << " | while read I; do" << endl;
            res << "  ivar=`getInterfaceVarName $I`" << endl;
            res << "  " << getaddr_function_name << " $I $ivar" << endl;
            res << "  cmd=\"$\"$ivar"   << endl;
            res << "  eval \"addr=$cmd\""          << endl;
            cmdline.replace(p1,p2-p1,"$addr");
            res << "  test -n \"$addr\" && ";
            if (nlines>1) res << "{" << endl;
            res << cmdline;
            if (nlines>1) res << "}" << endl;
            res << "done" << endl;
        } else
        {
            // bug #1851166: there could be two dynamic interfaces in
            // the same rule. Just print "test" command here and continue
            // in the "while" loop. We'll print actual commands when the loop
            // ends.
            res << "test -n \"" << iface_var << "\" && ";
        }
        p1++;  // p1 points at the previous "$i_" fragment
    }


    // for wildcard interfaces we only support one such interface
    // per rule and we have already printed the actual command above.
    if (!wildcard_interfaces)
    {
        if (nlines>1) res << "{" << endl;
        res << command_line;
        if (nlines>1) res << "}" << endl;
    }

    return res.str();
}

string OSConfigurator_linux24::printIPForwardingCommands()
{
/* Turn on packet forwarding if we have to */
    // string os_family = Resources::os_res[fw->getStr("host_OS")]->
    //     getResourceStr("/FWBuilderResources/Target/family");
    FWOptions* options = fw->getOptionsObject();
    Configlet ip_forwarding(fw, "linux24", "ip_forwarding");
    ip_forwarding.removeComments();
    ip_forwarding.collapseEmptyStrings(true);

    string s = options->getStr("linux24_ip_forward");
    ip_forwarding.setVariable("ipv4", !s.empty());
    ip_forwarding.setVariable("ipv4_forw", (s=="1" || s=="On" || s=="on")?1:0);

    s = options->getStr("linux24_ipv6_forward");
    ip_forwarding.setVariable("ipv6", !s.empty());
    ip_forwarding.setVariable("ipv6_forw", (s=="1" || s=="On" || s=="on")?1:0);

    return ip_forwarding.expand().toStdString();
}

void OSConfigurator_linux24::epilog()
{
}

list<std::string> OSConfigurator_linux24::getGeneratedFiles() const
{
    list<string> list;
    return list;
}
