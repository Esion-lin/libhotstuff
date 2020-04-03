/**
 * Copyright 2018 VMware
 * Copyright 2018 Ted Yin
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <iostream>
#include <cstring>
#include <cassert>
#include <algorithm>
#include <random>
#include <unistd.h>
#include <signal.h>

#include "salticidae/stream.h"
#include "salticidae/util.h"
#include "salticidae/network.h"
#include "salticidae/msg.h"

#include "hotstuff/promise.hpp"
#include "hotstuff/type.h"
#include "hotstuff/entity.h"
#include "hotstuff/util.h"
#include "hotstuff/client.h"
#include "hotstuff/hotstuff.h"
#include "hotstuff/liveness.h"

using salticidae::MsgNetwork;
using salticidae::ClientNetwork;
using salticidae::ElapsedTime;
using salticidae::Config;
using salticidae::_1;
using salticidae::_2;
using salticidae::static_pointer_cast;
using salticidae::trim_all;
using salticidae::split;

using hotstuff::TimerEvent;
using hotstuff::EventContext;
using hotstuff::NetAddr;
using hotstuff::HotStuffError;
using hotstuff::CommandDummy;
using hotstuff::Finality;
using hotstuff::command_t;
using hotstuff::uint256_t;
using hotstuff::opcode_t;
using hotstuff::bytearray_t;
using hotstuff::DataStream;
using hotstuff::ReplicaID;
using hotstuff::MsgReqCmd;
using hotstuff::MsgRespCmd;
using hotstuff::get_hash;
using hotstuff::promise_t;

using HotStuff = hotstuff::HotStuffSecp256k1;

class HotStuffApp: public HotStuff {
    double stat_period;
    double impeach_timeout;
    EventContext ec;

    /** Timer object to schedule a periodic printing of system statistics */
    TimerEvent ev_stat_timer;
    /** Timer object to monitor the progress for simple impeachment */
    TimerEvent impeach_timer;
    /** The listen address for client RPC */

    std::unordered_map<const uint256_t, promise_t> unconfirmed;
   

    /* for the dedicated thread sending responses to the clients */

    static command_t parse_cmd(DataStream &s) {
        auto cmd = new CommandDummy();
        s >> *cmd;
        return cmd;
    }

    void reset_imp_timer() {
        impeach_timer.del();
        impeach_timer.add(impeach_timeout);
    }

    void state_machine_execute(const Finality &fin) override {
        reset_imp_timer();
#ifndef HOTSTUFF_ENABLE_BENCHMARK
        HOTSTUFF_LOG_INFO("replicated %s", std::string(fin).c_str());
#endif
    }


    public:
    HotStuffApp(uint32_t blk_size,
                double stat_period,
                double impeach_timeout,
                ReplicaID idx,
                const bytearray_t &raw_privkey,
                NetAddr plisten_addr,
                NetAddr clisten_addr,
                hotstuff::pacemaker_bt pmaker,
                const EventContext &ec,
                size_t nworker,
                const Net::Config &repnet_config,
                const ClientNetwork<opcode_t>::Config &clinet_config);

    void start(const std::vector<std::tuple<NetAddr, bytearray_t, bytearray_t>> &reps);
    void stop();
};

std::pair<std::string, std::string> split_ip_port_cport(const std::string &s) {
    auto ret = trim_all(split(s, ";"));
    if (ret.size() != 2)
        throw std::invalid_argument("invalid cport format");
    return std::make_pair(ret[0], ret[1]);
}

salticidae::BoxObj<HotStuffApp> papp = nullptr;

int main(int argc, char **argv) {
    Config config("hotstuff.conf");

    ElapsedTime elapsed;
    elapsed.start();

    auto opt_blk_size = Config::OptValInt::create(6);
    auto opt_parent_limit = Config::OptValInt::create(-1);
    auto opt_stat_period = Config::OptValDouble::create(10);
    auto opt_replicas = Config::OptValStrVec::create();
    auto opt_idx = Config::OptValInt::create(0);
    auto opt_client_port = Config::OptValInt::create(-1);
    auto opt_privkey = Config::OptValStr::create();
    auto opt_tls_privkey = Config::OptValStr::create();
    auto opt_coo_listen_port = Config::OptValInt::create(10060);
    auto opt_coo_send_port = Config::OptValInt::create(10000);
    auto opt_iri_listen_port = Config::OptValInt::create(15260);
    auto opt_iri_send_port = Config::OptValInt::create(13260);
    auto opt_tls_cert = Config::OptValStr::create();
    auto opt_help = Config::OptValFlag::create(false);
    auto opt_pace_maker = Config::OptValStr::create("dummy");
    auto opt_fixed_proposer = Config::OptValInt::create(1);
    auto opt_base_timeout = Config::OptValDouble::create(1);
    auto opt_prop_delay = Config::OptValDouble::create(1);
    auto opt_imp_timeout = Config::OptValDouble::create(11);
    auto opt_nworker = Config::OptValInt::create(1);
    auto opt_repnworker = Config::OptValInt::create(1);
    auto opt_repburst = Config::OptValInt::create(100);
    auto opt_clinworker = Config::OptValInt::create(8);
    auto opt_cliburst = Config::OptValInt::create(1000);
    auto opt_notls = Config::OptValFlag::create(false);

    config.add_opt("block-size", opt_blk_size, Config::SET_VAL);
    config.add_opt("parent-limit", opt_parent_limit, Config::SET_VAL);
    config.add_opt("stat-period", opt_stat_period, Config::SET_VAL);
    config.add_opt("replica", opt_replicas, Config::APPEND, 'a', "add an replica to the list");
    config.add_opt("idx", opt_idx, Config::SET_VAL, 'i', "specify the index in the replica list");
    config.add_opt("cport", opt_client_port, Config::SET_VAL, 'c', "specify the port listening for clients");
    config.add_opt("coo_listen_port", opt_coo_listen_port, Config::SET_VAL);
    config.add_opt("coo_send_port", opt_coo_send_port, Config::SET_VAL);
    config.add_opt("iri_listen_port", opt_iri_listen_port, Config::SET_VAL);
    config.add_opt("iri_send_port", opt_iri_send_port, Config::SET_VAL);
    config.add_opt("privkey", opt_privkey, Config::SET_VAL);
    config.add_opt("tls-privkey", opt_tls_privkey, Config::SET_VAL);
    config.add_opt("tls-cert", opt_tls_cert, Config::SET_VAL);
    config.add_opt("pace-maker", opt_pace_maker, Config::SET_VAL, 'p', "specify pace maker (dummy, rr)");
    config.add_opt("proposer", opt_fixed_proposer, Config::SET_VAL, 'l', "set the fixed proposer (for dummy)");
    config.add_opt("base-timeout", opt_base_timeout, Config::SET_VAL, 't', "set the initial timeout for the Round-Robin Pacemaker");
    config.add_opt("prop-delay", opt_prop_delay, Config::SET_VAL, 't', "set the delay that follows the timeout for the Round-Robin Pacemaker");
    config.add_opt("imp-timeout", opt_imp_timeout, Config::SET_VAL, 'u', "set impeachment timeout (for sticky)");
    config.add_opt("nworker", opt_nworker, Config::SET_VAL, 'n', "the number of threads for verification");
    config.add_opt("repnworker", opt_repnworker, Config::SET_VAL, 'm', "the number of threads for replica network");
    config.add_opt("repburst", opt_repburst, Config::SET_VAL, 'b', "");
    config.add_opt("clinworker", opt_clinworker, Config::SET_VAL, 'M', "the number of threads for client network");
    config.add_opt("cliburst", opt_cliburst, Config::SET_VAL, 'B', "");
    config.add_opt("notls", opt_notls, Config::SWITCH_ON, 's', "disable TLS");
    config.add_opt("help", opt_help, Config::SWITCH_ON, 'h', "show this help info");

    EventContext ec;
    config.parse(argc, argv);
    if (opt_help->get())
    {
        config.print_help();
        exit(0);
    }
    auto idx = opt_idx->get();
    auto client_port = opt_client_port->get();
    std::vector<std::tuple<std::string, std::string, std::string>> replicas;
    for (const auto &s: opt_replicas->get())
    {
        auto res = trim_all(split(s, ","));
        if (res.size() != 3)
            throw HotStuffError("invalid replica info");
        replicas.push_back(std::make_tuple(res[0], res[1], res[2]));
    }

    if (!(0 <= idx && (size_t)idx < replicas.size()))
        throw HotStuffError("replica idx out of range");
    std::string binding_addr = std::get<0>(replicas[idx]);
    if (client_port == -1)
    {
        auto p = split_ip_port_cport(binding_addr);
        size_t idx;
        try {
            client_port = stoi(p.second, &idx);
        } catch (std::invalid_argument &) {
            throw HotStuffError("client port not specified");
        }
    }

    NetAddr plisten_addr{split_ip_port_cport(binding_addr).first};

    auto parent_limit = opt_parent_limit->get();
    hotstuff::pacemaker_bt pmaker;
    if (opt_pace_maker->get() == "dummy")
        pmaker = new hotstuff::PaceMakerDummyFixed(opt_fixed_proposer->get(), parent_limit);
    else
        pmaker = new hotstuff::PaceMakerRR(ec, parent_limit, opt_base_timeout->get(), opt_prop_delay->get());

    HotStuffApp::Net::Config repnet_config;
    ClientNetwork<opcode_t>::Config clinet_config;
    if (!opt_tls_privkey->get().empty() && !opt_notls->get())
    {
        auto tls_priv_key = new salticidae::PKey(
                salticidae::PKey::create_privkey_from_der(
                    hotstuff::from_hex(opt_tls_privkey->get())));
        auto tls_cert = new salticidae::X509(
                salticidae::X509::create_from_der(
                    hotstuff::from_hex(opt_tls_cert->get())));
        repnet_config
            .enable_tls(true)
            .tls_key(tls_priv_key)
            .tls_cert(tls_cert);
    }
    repnet_config
        .burst_size(opt_repburst->get())
        .nworker(opt_repnworker->get());
    clinet_config
        .burst_size(opt_cliburst->get())
        .nworker(opt_clinworker->get());
    papp = new HotStuffApp(opt_blk_size->get(),
                        opt_stat_period->get(),
                        opt_imp_timeout->get(),
                        idx,
                        hotstuff::from_hex(opt_privkey->get()),
                        plisten_addr,
                        NetAddr("0.0.0.0", client_port),
                        std::move(pmaker),
                        ec,
                        opt_nworker->get(),
                        repnet_config,
                        clinet_config);
    papp->listen_port_for_coo = opt_coo_listen_port.get()->get();
    papp->send_port_for_coo = opt_coo_send_port.get()->get();
    papp->listen_port_for_iri = opt_iri_listen_port.get()->get();
    papp->send_port_for_iri = opt_iri_send_port.get()->get();
    HOTSTUFF_LOG_INFO("opt_coo_listen_port is %d\n", opt_coo_listen_port.get()->get());
    std::vector<std::tuple<NetAddr, bytearray_t, bytearray_t>> reps;
    for (auto &r: replicas)
    {
        auto p = split_ip_port_cport(std::get<0>(r));
        reps.push_back(std::make_tuple(
            NetAddr(p.first),
            hotstuff::from_hex(std::get<1>(r)),
            hotstuff::from_hex(std::get<2>(r))));
    }
    auto shutdown = [&](int) { papp->stop(); };
    salticidae::SigEvent ev_sigint(ec, shutdown);
    salticidae::SigEvent ev_sigterm(ec, shutdown);
    ev_sigint.add(SIGINT);
    ev_sigterm.add(SIGTERM);

    papp->start(reps);
    elapsed.stop(true);
    return 0;
}

HotStuffApp::HotStuffApp(uint32_t blk_size,
                        double stat_period,
                        double impeach_timeout,
                        ReplicaID idx,
                        const bytearray_t &raw_privkey,
                        NetAddr plisten_addr,
                        NetAddr clisten_addr,
                        hotstuff::pacemaker_bt pmaker,
                        const EventContext &ec,
                        size_t nworker,
                        const Net::Config &repnet_config,
                        const ClientNetwork<opcode_t>::Config &clinet_config):
    HotStuff(blk_size, idx, raw_privkey,
            plisten_addr, std::move(pmaker), ec, nworker, repnet_config),
    stat_period(stat_period),
    impeach_timeout(impeach_timeout),
    ec(ec){

}


void HotStuffApp::start(const std::vector<std::tuple<NetAddr, bytearray_t, bytearray_t>> &reps) {
    ev_stat_timer = TimerEvent(ec, [this](TimerEvent &) {
        HotStuff::print_stat();
        //HotStuffCore::prune(100);
        ev_stat_timer.add(stat_period);
    });
    ev_stat_timer.add(stat_period);
    impeach_timer = TimerEvent(ec, [this](TimerEvent &) {
        get_pace_maker()->impeach();
        reset_imp_timer();
    });
    impeach_timer.add(impeach_timeout);
    HOTSTUFF_LOG_INFO("** starting the system with parameters **");
    HOTSTUFF_LOG_INFO("blk_size = %lu", blk_size);
    HOTSTUFF_LOG_INFO("conns = %lu", HotStuff::size());
    HOTSTUFF_LOG_INFO("** starting the event loop...");
    HotStuff::start(reps);
   
    ec.dispatch();
}

void HotStuffApp::stop() {
    ec.stop();
}

