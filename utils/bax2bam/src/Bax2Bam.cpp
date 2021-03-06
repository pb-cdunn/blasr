// Author: Derek Barnett

#include "Bax2Bam.h"
#include "CcsConverter.h"
#include "HqRegionConverter.h"
#include "PolymeraseReadConverter.h"
#include "SubreadConverter.h"
#include <pbbam/DataSet.h>
#include <boost/algorithm/string.hpp>
#include <memory>
#include <fstream>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <time.h>

#include <unistd.h> // getcwd
using namespace std;

namespace internal {

static inline
string CurrentWorkingDir(void)
{
    char result[FILENAME_MAX] = { };
    if (getcwd(result, FILENAME_MAX) == nullptr)
        return string();
    return string(result);
}

static
bool WriteDatasetXmlOutput(const Settings& settings,
                           vector<string>* errors)
{
    using namespace PacBio::BAM;
    assert(errors);

    try {
        DataSet dataset(settings.datasetXmlFilename);
        assert(dataset.Type() == DataSet::HDF_SUBREAD);

        // change type
        dataset.Type(DataSet::SUBREAD);
        dataset.MetaType("PacBio.DataSet.SubreadSet");

        time_t currentTime = time(NULL);
        //const string& timestamp = CurrentTimestamp();
        dataset.CreatedAt(ToIso8601(currentTime));
        dataset.TimeStampedName(string{"pacbio_dataset_subreadset-"}+ToDataSetFormat(currentTime));

        // change files: remove BAX, add BAM
        std::vector<ExternalResource> toRemove;
        ExternalResources resources = dataset.ExternalResources();
        auto iter = resources.cbegin();
        auto end  = resources.cend();
        for (; iter != end; ++iter) {
            ExternalResource e = (*iter);
            boost::iterator_range<string::iterator> baxFound = boost::algorithm::ifind_first(e.MetaType(), "bax");
            if (!baxFound.empty()) 
                toRemove.push_back(e);
        }

        while(!toRemove.empty()) {
            auto e = toRemove.back();
            resources.Remove(e);
            toRemove.pop_back();
        }

        const string scheme = "file://";
        string mainBamFilepath;

        // If the output filename starts with a slash, assume it's the path
        if (boost::starts_with(settings.outputBamFilename, "/"))
        {
            mainBamFilepath = settings.outputBamFilename;
        }
        else // otherwise build the path from the CWD
        { 
            mainBamFilepath = CurrentWorkingDir();
            if (!mainBamFilepath.empty())
                mainBamFilepath.append(1, '/');
            mainBamFilepath.append(settings.outputBamFilename);
        }

        // Combine the scheme and filepath and store in the dataset
        mainBamFilepath = scheme + mainBamFilepath;
        ExternalResource mainBam{ "PacBio.SubreadFile.SubreadBamFile", mainBamFilepath };
        FileIndex mainPbi{ "PacBio.Index.PacBioIndex", mainBamFilepath + ".pbi" };
        mainBam.FileIndices().Add(mainPbi);

        // maybe add scraps BAM (& PBI)
        if (!settings.scrapsBamFilename.empty()) {

            string scrapsBamFilepath;

            // If the output filename starts with a slash, assume it's the path
            if (boost::starts_with(settings.scrapsBamFilename, "/"))
            {
                scrapsBamFilepath = settings.scrapsBamFilename;
            }
            else // otherwise build the path from the CWD
            {
                scrapsBamFilepath = CurrentWorkingDir();
                if (!scrapsBamFilepath.empty())
                    scrapsBamFilepath.append(1, '/');
                scrapsBamFilepath.append(settings.scrapsBamFilename);
            }

            ExternalResource scrapsBam{ "PacBio.SubreadFile.ScrapsBamFile", scrapsBamFilepath };
            FileIndex scrapsPbi{ "PacBio.Index.PacBioIndex", scrapsBamFilepath + ".pbi" };
            scrapsBam.FileIndices().Add(scrapsPbi);
            mainBam.ExternalResources().Add(scrapsBam);
        }

        // add resources to output dataset
        resources.Add(mainBam);
        dataset.ExternalResources(resources);

        // save to file 
        string xmlFn = settings.outputXmlFilename; // try user-provided explicit filename first
        if (xmlFn.empty())
            xmlFn = settings.outputBamPrefix + ".dataset.xml"; // prefix set w/ moviename elsewhere if not user-provided
        dataset.Save(xmlFn);
        return true;

    } catch (std::exception&) {
        errors->push_back("could not create output XML");
        return false;
    }
}

} // namespace internal

int Bax2Bam::Run(Settings& settings) {

    // init conversion mode
    std::unique_ptr<IConverter> converter;
    switch (settings.mode) {
        case Settings::HQRegionMode   : converter.reset(new HqRegionConverter(settings)); break;
        case Settings::PolymeraseMode : converter.reset(new PolymeraseReadConverter(settings)); break;
        case Settings::SubreadMode    : converter.reset(new SubreadConverter(settings)); break;
        case Settings::CCSMode        : converter.reset(new CcsConverter(settings)); break;
        default :
            cerr << "ERROR: unknown mode selected" << endl;
            return EXIT_FAILURE;
    }

    // run conversion
    bool success = false;
    vector<string> xmlErrors;
    if (converter->Run()) {
        success = true;

        // if given dataset XML as input, attempt write dataset XML output
        if (!settings.datasetXmlFilename.empty()) {
            if (!internal::WriteDatasetXmlOutput(settings, &xmlErrors))
                success = false;
        }
    }

    // return success/fail
    if (success)
        return EXIT_SUCCESS;
    else {
        for (const string& e : converter->Errors())
            cerr << "ERROR: " << e << endl;
        for (const string& e : xmlErrors)
            cerr << "ERROR: " << e << endl;
        return EXIT_FAILURE;
    }
}
