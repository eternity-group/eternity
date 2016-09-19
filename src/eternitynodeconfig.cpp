
#include "net.h"
#include "eternitynodeconfig.h"
#include "util.h"
#include "ui_interface.h"
#include <base58.h>

CEternitynodeConfig eternitynodeConfig;

void CEternitynodeConfig::add(std::string alias, std::string ip, std::string privKey, std::string txHash, std::string outputIndex) {
    CEternitynodeEntry cme(alias, ip, privKey, txHash, outputIndex);
    entries.push_back(cme);
}

bool CEternitynodeConfig::read(std::string& strErr) {
    int linenumber = 1;
    boost::filesystem::path pathEternitynodeConfigFile = GetEternitynodeConfigFile();
    boost::filesystem::ifstream streamConfig(pathEternitynodeConfigFile);

    if (!streamConfig.good()) {
        FILE* configFile = fopen(pathEternitynodeConfigFile.string().c_str(), "a");
        if (configFile != NULL) {
            std::string strHeader = "# Eternitynode config file\n"
                          "# Format: alias IP:port eternitynodeprivkey collateral_output_txid collateral_output_index\n"
                          "# Example: en1 127.0.0.2:14855 93HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg 2bcd3c84c84f87eaa86e4e56834c92927a07f9e18718810b92e0d0324456a67c 0\n";
            fwrite(strHeader.c_str(), std::strlen(strHeader.c_str()), 1, configFile);
            fclose(configFile);
        }
        return true; // Nothing to read, so just return
    }

    for(std::string line; std::getline(streamConfig, line); linenumber++)
    {
        if(line.empty()) continue;

        std::istringstream iss(line);
        std::string comment, alias, ip, privKey, txHash, outputIndex;

        if (iss >> comment) {
            if(comment.at(0) == '#') continue;
            iss.str(line);
            iss.clear();
        }

        if (!(iss >> alias >> ip >> privKey >> txHash >> outputIndex)) {
            iss.str(line);
            iss.clear();
            if (!(iss >> alias >> ip >> privKey >> txHash >> outputIndex)) {
                strErr = _("Could not parse eternitynode.conf") + "\n" +
                        strprintf(_("Line: %d"), linenumber) + "\n\"" + line + "\"";
                streamConfig.close();
                return false;
            }
        }

        if(Params().NetworkID() == CBaseChainParams::MAIN) {
            if(CService(ip).GetPort() != 4855) {
                strErr = _("Invalid port detected in eternitynode.conf") + "\n" +
                        strprintf(_("Line: %d"), linenumber) + "\n\"" + line + "\"" + "\n" +
                        _("(must be 4855 for mainnet)");
                streamConfig.close();
                return false;
            }
        } else if(CService(ip).GetPort() == 4855) {
            strErr = _("Invalid port detected in eternitynode.conf") + "\n" +
                    strprintf(_("Line: %d"), linenumber) + "\n\"" + line + "\"" + "\n" +
                    _("(4855 could be used only on mainnet)");
            streamConfig.close();
            return false;
        }


        add(alias, ip, privKey, txHash, outputIndex);
    }

    streamConfig.close();
    return true;
}
