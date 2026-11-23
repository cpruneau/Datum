/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file HerwigEventGenerator.cpp
//! @brief Implementation of HerwigEventGenerator (through Herwig::API and ThePEG).
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "HerwigEventGenerator.hpp"
#include "Event.hpp"
#include "Exceptions.hpp"
#include "FileSystem.hpp"
#include "Folder.hpp"
#include "LongParameter.hpp"
#include "Particle.hpp"
#include "ParticleDb.hpp"
#include "ParticleFilter.hpp"
#include "ParticleType.hpp"
#include "PrintHelpers.hpp"
#include "StringParameter.hpp"
// Herwig and ThePEG last (their names stay in their namespaces)
#include "Herwig/API/HerwigAPI.h"
#include "Herwig/API/HerwigUI.h"
#include "ThePEG/EventRecord/Event.h"
#include "ThePEG/EventRecord/Particle.h"
#include "ThePEG/Repository/EventGenerator.h"

namespace Datum
{

//! Number of free-form command keys (<name>:Option0 ... <name>:Option19).
static const int kNHerwigOptions = 20;

namespace
{
//!
//! The settings Herwig::API reads, as its command line gives them (Herwig::HerwigCLI): a run mode, the repository,
//! the input or run file, a setup file, a seed and a tag. quit() and quitWithHelp() throw instead of ending the job.
//!
class HerwigSettings : public Herwig::HerwigUI
{
public:
  HerwigSettings(Herwig::RunMode::Mode mode, const std::string & repository, const std::string & input,
                 const std::string & setup="", int seed=0, const std::string & tag="",
                 const std::vector<std::string> & readDirectories=std::vector<std::string>())
  : _mode(mode), _repository(repository), _input(input), _setup(setup), _seed(seed), _tag(tag),
    _read(readDirectories) { }
  Herwig::RunMode::Mode runMode() const override { return _mode; }
  std::string repository() const override { return _repository; }
  std::string inputfile() const override { return _input; }
  std::string setupfile() const override { return _setup; }
  bool resume() const override { return false; }
  bool tics() const override { return false; }
  std::string tag() const override { return _tag; }
  std::string integrationList() const override { return ""; }
  const std::vector<std::string> & prependReadDirectories() const override { return _read; }
  const std::vector<std::string> & appendReadDirectories() const override { return _none; }
  long N() const override { return 0; }
  int seed() const override { return _seed; }
  int jobs() const override { return 1; }
  unsigned int jobSize() const override { return 1; }
  unsigned int maxJobs() const override { return 1; }
  void quitWithHelp() const override { quit(); }
  void quit() const override
  {
    throw Exception("Herwig stopped (see its messages above, and its .log file)", "HerwigSettings::quit");
  }
  std::ostream & outStream() const override { return std::cout; }
  std::ostream & errStream() const override { return std::cerr; }
  std::istream & inStream() const override { return std::cin; }
private:
  Herwig::RunMode::Mode _mode;
  std::string _repository, _input, _setup;
  int _seed;
  std::string _tag;
  std::vector<std::string> _read;
  std::vector<std::string> _none;
};

//! Back to the current directory when it goes out of scope.
class DirectoryGuard
{
public:
  explicit DirectoryGuard(const std::filesystem::path & directory) : _previous(std::filesystem::current_path())
  {
    if (!directory.empty()) std::filesystem::current_path(directory);
  }
  ~DirectoryGuard() { std::error_code e; std::filesystem::current_path(_previous, e); }
private:
  std::filesystem::path _previous;
};

//! Name of the run file an input file saves: <name>.run of its last command "saverun <name> <generator>" ("" if none).
std::string savedRunName(const std::filesystem::path & input)
{
  std::ifstream in(input);
  std::string line, name;
  while (std::getline(in, line))
    {
    std::istringstream words(line.substr(0, line.find('#')));
    std::string command, run;
    if (words >> command >> run && command == "saverun") name = run + ".run";
    }
  return name;
}

//! A momentum or mass in GeV as a repository value (e.g. 12.5*GeV).
std::string gev(double value)
{
  char text[64];
  std::snprintf(text, sizeof(text), "%.17g*GeV", value);
  return text;
}

//! Repository commands restricting the hard process to the pT-hat range [ptMin, ptMax) (negative: not set): the kT of
//! the outgoing partons (JetKtCut), and the mass of the hard process lowered to 2 ptMin (it is at least 2 pT-hat).
std::vector<std::string> ptHatCommands(double ptMin, double ptMax)
{
  std::vector<std::string> commands;
  if (ptMin>=0.0)
    {
    commands.push_back("set /Herwig/Cuts/JetKtCut:MinKT " + gev(ptMin));
    commands.push_back("set /Herwig/Cuts/QCDCuts:MHatMin " + gev(2.0*ptMin));
    }
  if (ptMax>=0.0) commands.push_back("set /Herwig/Cuts/JetKtCut:MaxKT " + gev(ptMax));
  return commands;
}

//! Text of an input file with the commands inserted before its last saverun command, which then saves <run>_pthat;
//! the name of that run file is returned in runName ("" if the file has no saverun command).
std::string ptHatInput(const std::filesystem::path & input, const std::vector<std::string> & commands,
                       std::string & runName)
{
  std::ifstream in(input);
  std::vector<std::string> lines;
  std::string line;
  int last = -1;
  while (std::getline(in, line))
    {
    std::istringstream words(line.substr(0, line.find('#')));
    std::string command;
    if (words >> command && command == "saverun") last = int(lines.size());
    lines.push_back(line);
    }
  runName.clear();
  if (last<0) return "";
  std::istringstream words(lines[last].substr(0, lines[last].find('#')));
  std::string command, run, generator;
  words >> command >> run >> generator;
  std::ostringstream out;
  for (int k=0; k<int(lines.size()); k++)
    {
    if (k!=last) { out << lines[k] << "\n"; continue; }
    out << "# Datum: pT-hat range of the hard process (HerwigEventGenerator PTHAT_MIN, PTHAT_MAX)\n";
    out << "cd /\n";
    for (const std::string & c : commands) out << c << "\n";
    out << "saverun " << run << "_pthat " << generator << "\n";
    }
  runName = run + "_pthat.run";
  return out.str();
}

//! Content of a file ("" if it cannot be read).
std::string fileText(const std::filesystem::path & path)
{
  std::ifstream in(path);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}
} // namespace

//! The event generator of Herwig (a ThePEG::EventGenerator, reference counted).
class HerwigRun
{
public:
  ThePEG::EGPtr generator;
};

HerwigEventGenerator::HerwigEventGenerator()
:
EventProcessor(),
herwig(nullptr),
savePhotons(true),
saveNeutrinos(true),
nWeighted(0),
nFailed(0),
nAccepted(0),
ptHatMin(-1.0),
ptHatMax(-1.0),
sigmaGen(-1.0),
sigmaErr(0.0)
{
  appendClassName("HerwigEventGenerator");
  setMinimumReportLevel(Object::kInfo);
  setName("HerwigEventGenerator");
  setTitle("HerwigEventGenerator");
}

HerwigEventGenerator::HerwigEventGenerator(const HerwigEventGenerator & source)
:
EventProcessor(source),
herwig(nullptr),
savePhotons(source.savePhotons),
saveNeutrinos(source.saveNeutrinos),
nWeighted(0),
nFailed(0),
nAccepted(0),
ptHatMin(source.ptHatMin),
ptHatMax(source.ptHatMax),
sigmaGen(source.sigmaGen),
sigmaErr(source.sigmaErr)
{ }

HerwigEventGenerator::~HerwigEventGenerator()
{
  delete herwig;
}

HerwigEventGenerator & HerwigEventGenerator::operator=(const HerwigEventGenerator & rhs)
{
  if (this!=&rhs)
    {
    EventProcessor::operator=(rhs);
    savePhotons   = rhs.savePhotons;
    saveNeutrinos = rhs.saveNeutrinos;
    ptHatMin = rhs.ptHatMin;  ptHatMax = rhs.ptHatMax;
    sigmaGen = rhs.sigmaGen;  sigmaErr = rhs.sigmaErr;
    }
  return *this;
}

void HerwigEventGenerator::setDefaultConfiguration()
{
  if (reportDebug(__FUNCTION__)) { printCR(); }
  EventProcessor::setDefaultConfiguration();
  String taskName = name();
  _configuration.addProperty(createKey(taskName,"InputFile"),        String(""));
  _configuration.addProperty(createKey(taskName,"RunFile"),          String("LHC.run"));
  _configuration.addProperty(createKey(taskName,"Repository"),       String("HerwigDefaults.rpo"));
  _configuration.addProperty(createKey(taskName,"SetupFile"),        String(""));
  _configuration.addProperty(createKey(taskName,"Tag"),              String(""));
  _configuration.addProperty(createKey(taskName,"SetSeed"),          true);
  _configuration.addProperty(createKey(taskName,"SeedValue"),        121211);
  _configuration.addProperty(createKey(taskName,"SavePhotons"),      true);
  _configuration.addProperty(createKey(taskName,"SaveNeutrinos"),    true);
  _configuration.addProperty(createKey(taskName,"Print:Statistics"), true);
  _configuration.addProperty(createKey(taskName,"PTHAT_MIN"),        -1.0);
  _configuration.addProperty(createKey(taskName,"PTHAT_MAX"),        -1.0);
  for (int k=0; k<kNHerwigOptions; k++)
    {
    String option = "Option"; option += k;
    _configuration.addProperty(createKey(taskName,option), String("none"));
    }
}

void HerwigEventGenerator::initialize()
{
  if (reportStart(__FUNCTION__)) { printCR(); }
  EventProcessor::initialize();
  namespace fs = std::filesystem;
  String taskName = name();
  delete herwig;
  herwig = nullptr;
  typeOfCode.clear();
  skippedCodes.clear();
  nWeighted = 0;
  nFailed   = 0;
  nAccepted = 0;
  sigmaGen  = -1.0;
  sigmaErr  = 0.0;
  ptHatMin = _configuration.valueDouble(createKey(taskName,"PTHAT_MIN"),true,-1.0);
  ptHatMax = _configuration.valueDouble(createKey(taskName,"PTHAT_MAX"),true,-1.0);
  if (ptHatMin>=0.0 && ptHatMax>=0.0 && !(ptHatMax>ptHatMin))
    throw ConfigurationException(createKey(taskName,"PTHAT_MAX"),"PTHAT_MAX must be above PTHAT_MIN",__FUNCTION__);
  const std::vector<std::string> ptHatCuts = ptHatCommands(ptHatMin, ptHatMax);
  if (ptHatMin>=0.0) printValue("Herwig:PTHAT_MIN (JetKtCut:MinKT)",ptHatMin);
  if (ptHatMax>=0.0) printValue("Herwig:PTHAT_MAX (JetKtCut:MaxKT)",ptHatMax);
  savePhotons   = _configuration.valueBool(createKey(taskName,"SavePhotons"));
  saveNeutrinos = _configuration.valueBool(createKey(taskName,"SaveNeutrinos"));
  std::string repository = FileSystem::expand(_configuration.valueString(createKey(taskName,"Repository"))).str();
  std::string input      = FileSystem::expand(_configuration.valueString(createKey(taskName,"InputFile"))).str();
  std::string runFile    = FileSystem::expand(_configuration.valueString(createKey(taskName,"RunFile"))).str();
  std::string tag        = _configuration.valueString(createKey(taskName,"Tag")).str();
  int seed = _configuration.valueBool(createKey(taskName,"SetSeed"))
             ? int(_configuration.valueLong(createKey(taskName,"SeedValue"))) : 0;

  // an input file (.in) given as the run file is taken as the input file
  bool noInput = input.empty() || input=="none" || input=="NONE";
  if (noInput && fs::path(runFile).extension()==".in")
    {
    input   = runFile;
    noInput = false;
    printValue("Herwig input file (given as RunFile)",String(input.c_str()));
    }
  // the run file: read from the input file (Herwig read) when it is missing or older
  if (!noInput)
    {
    fs::path in = fs::absolute(input);
    if (!fs::is_regular_file(in))
      throw ConfigurationException(createKey(taskName,"InputFile"), String("No Herwig input file ") + String(in.c_str()),
                                   __FUNCTION__);
    std::string saved = savedRunName(in);
    if (saved.empty())
      throw ConfigurationException(createKey(taskName,"InputFile"),
                                   String("The Herwig input file has no saverun command: ") + String(in.c_str()),
                                   __FUNCTION__);
    fs::path run = in.parent_path() / saved;
    if (!ptHatCuts.empty())
      {
      // the input file with the cuts of the pT-hat range, read in the current directory (the job's), its directory
      // added to those read from (its own read commands)
      std::string ptHatRun;
      std::string text = ptHatInput(in, ptHatCuts, ptHatRun);
      fs::path ptHatIn = fs::absolute(std::string(taskName.str()) + "_pthat.in");
      run = fs::absolute(ptHatRun);
      bool same = fileText(ptHatIn)==text;
      if (!same)
        {
        std::ofstream out(ptHatIn);
        out << text;
        if (!out) throw Exception(String("Cannot write ") + String(ptHatIn.c_str()),__FUNCTION__);
        }
      if (!same || !fs::is_regular_file(run) || fs::last_write_time(run) < fs::last_write_time(ptHatIn))
        {
        printValue("Herwig read (pT-hat range)",String(ptHatIn.c_str()));
        Herwig::API::read(HerwigSettings(Herwig::RunMode::READ, repository, ptHatIn.string(), "", 0, "",
                                         std::vector<std::string>{in.parent_path().string()}));
        if (!fs::is_regular_file(run))
          throw Exception(String("Herwig read did not make the run file ") + String(run.c_str()),__FUNCTION__);
        }
      }
    else if (!fs::is_regular_file(run) || fs::last_write_time(run) < fs::last_write_time(in))
      {
      printValue("Herwig read",String(in.c_str()));
      DirectoryGuard here(in.parent_path());          // Herwig saves the run file in the current directory
      Herwig::API::read(HerwigSettings(Herwig::RunMode::READ, repository, in.filename().string()));
      if (!fs::is_regular_file(run))
        throw Exception(String("Herwig read did not make the run file ") + String(run.c_str()),__FUNCTION__);
      }
    runFile = run.string();
    }
  if (!fs::is_regular_file(runFile))
    throw ConfigurationException(createKey(taskName,"RunFile"),
                                 String("No Herwig run file ") + String(runFile.c_str())
                                 + " (make it with: Herwig read <input file>, or give InputFile)",__FUNCTION__);
  if (noInput && !ptHatCuts.empty() && reportWarning(__FUNCTION__))
    printValue("pT-hat range set at run time (Herwig run -x); the run file was integrated with its own cuts: "
               "prefer InputFile",String(runFile.c_str()));

  // the setup: the setup file and the commands of the options, in one file of the current directory
  std::string setup = FileSystem::expand(_configuration.valueString(createKey(taskName,"SetupFile"))).str();
  std::vector<std::string> commands;
  for (int k=0; k<kNHerwigOptions; k++)
    {
    String option = "Option"; option += k;
    String value  = _configuration.valueString(createKey(taskName,option));
    if (!value.isNull() && !value.contains("none"))
      {
      String label = "Herwig:"; label += option;
      printValue(label.data(),value);
      commands.push_back(value.str());
      }
    }
  if (noInput) commands.insert(commands.end(), ptHatCuts.begin(), ptHatCuts.end());   // no input file: at run time
  if (!commands.empty())
    {
    std::string combined = std::string(taskName.str()) + "_setup.in";
    std::ofstream out(combined);
    if (!setup.empty() && setup!="none" && setup!="NONE")
      {
      std::ifstream in(setup);
      if (!in) throw ConfigurationException(createKey(taskName,"SetupFile"),
                                            String("No Herwig setup file ") + String(setup.c_str()),__FUNCTION__);
      out << in.rdbuf() << "\n";
      }
    for (const std::string & command : commands) out << command << "\n";
    if (!out) throw Exception(String("Cannot write ") + String(combined.c_str()),__FUNCTION__);
    setup = combined;
    }
  else if (setup=="none" || setup=="NONE")
    setup = "";

  printValue("Herwig run file",String(runFile.c_str()));
  if (!setup.empty()) printValue("Herwig setup file",String(setup.c_str()));
  if (seed>0) printValue("Herwig seed",long(seed));
  herwig = new HerwigRun();
  herwig->generator = Herwig::API::prepareRun(HerwigSettings(Herwig::RunMode::RUN, repository, runFile, setup, seed,
                                                             tag));
  if (!herwig->generator)
    throw Exception(String("Herwig could not prepare the event generator of ") + String(runFile.c_str()),__FUNCTION__);
}

ParticleType * HerwigEventGenerator::particleType(int pdg)
{
  auto found = typeOfCode.find(pdg);
  if (found!=typeOfCode.end())
    {
    if (!found->second) skippedCodes[pdg]++;
    return found->second;
    }
  ParticleDb & particleDb = db();
  ParticleType * type = (particleDb.findIndexForPdgCode(pdg)>=0) ? particleDb.findPdgCode(pdg) : nullptr;
  typeOfCode[pdg] = type;
  if (!type) skippedCodes[pdg]++;
  return type;
}

void HerwigEventGenerator::execute()
{
  if (!herwig || !herwig->generator) throw Exception("Herwig is not initialized",__FUNCTION__);
  ThePEG::EventPtr source;
  try
    {
    source = herwig->generator->shoot();
    }
  catch (std::exception & error)
    {
    nFailed++;
    if (reportWarning(__FUNCTION__)) printValue("Herwig event failed (skipped)",String(error.what()));
    return;
    }
  if (!source)
    {
    nFailed++;
    return;
    }
  if (source->weight()!=1.0) nWeighted++;
  nAccepted++;
  Event & theEvent = event();
  ParticleFilter * particleFilter = particleFilters().empty() ? nullptr : particleFilters()[0];
  for (const ThePEG::tPPtr & p : source->getFinalState())
    {
    int pdg = int(p->id());
    printValue("HERWIG::execute() pdg",pdg);
    int absPdg = std::abs(pdg);
    if (!savePhotons   && pdg==22) continue;
    if (!saveNeutrinos && (absPdg==12 || absPdg==14 || absPdg==16 || absPdg==18)) continue;
    ParticleType * type = particleType(pdg);
    if (!type) continue;                         // not in the particle database (reported by finalize())
    printValue("HERWIG::execute() ","Got a type");
    const ThePEG::Lorentz5Momentum & momentum = p->momentum();
    const ThePEG::LorentzPoint vertex = p->labVertex();
    Particle & particle = Particle::factory().nextObject();
    particle.reset();
    particle.setType(type);
    particle.setLive(true);
//    printValue("HERWIG::execute() p.E=",momentum.e()/ThePEG::GeV);
//    printValue("HERWIG::execute() p.x=",momentum.x()/ThePEG::GeV);
//    printValue("HERWIG::execute() p.y=",momentum.y()/ThePEG::GeV);
//    printValue("HERWIG::execute() p.z=",momentum.z()/ThePEG::GeV);
    particle.setEPxPyPz(momentum.e()/ThePEG::GeV, momentum.x()/ThePEG::GeV, momentum.y()/ThePEG::GeV,
                        momentum.z()/ThePEG::GeV);
    particle.setXYZT(vertex.x()/ThePEG::mm, vertex.y()/ThePEG::mm, vertex.z()/ThePEG::mm, vertex.t()/ThePEG::mm);
    if (!particleFilter || particleFilter->accept(particle)) theEvent.addParticle(&particle);
    }
  _eventAccepted.increment();
  _taskExecuted.increment();
}

void HerwigEventGenerator::finalize()
{
  if (reportDebug(__FUNCTION__)) { printCR(); }
  if (!skippedCodes.empty() && reportWarning(__FUNCTION__))
    {
    printValue("Particles skipped: PDG codes not in the particle database",long(skippedCodes.size()));
    for (auto & entry : skippedCodes)
      {
      String label = "PDG code "; label += entry.first;
      printValue(label.data(),entry.second);
      }
    }
  if (nFailed>0 && reportWarning(__FUNCTION__)) printValue("Herwig events that failed (skipped)",nFailed);
  if (nWeighted>0 && reportWarning(__FUNCTION__))
    printValue((ptHatMin>=0.0 || ptHatMax>=0.0)
               ? "Weighted Herwig events (Datum ignores the weights: the pT-hat bins need unweighted events)"
               : "Weighted Herwig events (Datum ignores the weights)",nWeighted);
  if (herwig && herwig->generator)
    {
    sigmaGen = double(herwig->generator->integratedXSec()/ThePEG::millibarn);
    sigmaErr = double(herwig->generator->integratedXSecErr()/ThePEG::millibarn);
    if (_configuration.valueBool(createKey(name(),"Print:Statistics")))
      {
      printValue("Herwig events",long(herwig->generator->currentEventNumber()));
      printValue("Herwig cross section (pb)",double(herwig->generator->integratedXSec()/ThePEG::picobarn));
      printValue("Herwig cross section error (pb)",double(herwig->generator->integratedXSecErr()/ThePEG::picobarn));
      printValue("Herwig events accepted",nAccepted);
      }
    herwig->generator->finalize();                  // Herwig writes its .out file
    }
  delete herwig;
  herwig = nullptr;
  EventProcessor::finalize();
}

void HerwigEventGenerator::saveHistograms(Folder & top)
{
  if (sigmaGen>=0.0)
    {
    Folder * folder = top.folder(name());
    if (!folder) folder = top.addFolder(name());
    auto text = [](double value) { char s[64]; std::snprintf(s,sizeof(s),"%.17g",value); return String(s); };
    auto put = [&](const char * key, const String & value)
      { if (!folder->get<StringParameter>(key)) folder->add(new StringParameter(key,value),key); };
    put("DATUM_SIGMA_GEN",text(sigmaGen));
    put("DATUM_SIGMA_ERR",text(sigmaErr));
    put("DATUM_PTHAT_MIN",text(ptHatMin));
    put("DATUM_PTHAT_MAX",text(ptHatMax));
    if (!folder->get<LongParameter>("DATUM_N_ACCEPTED"))
      folder->add(new LongParameter("DATUM_N_ACCEPTED",nAccepted),"DATUM_N_ACCEPTED");
    }
  EventProcessor::saveHistograms(top);
}

} // namespace Datum
