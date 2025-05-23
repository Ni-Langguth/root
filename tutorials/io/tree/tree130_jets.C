/// \file
/// \ingroup tutorial_tree
///
/// Usage of a Tree using the JetEvent class.
///
/// The JetEvent class has several collections (TClonesArray)
/// and other collections (TRefArray) referencing objects
/// in the TClonesArrays.
/// The JetEvent class is in $ROOTSYS/tutorials/io/tree/JetEvent.h,cxx
/// to execute the script, do
/// ~~~
/// .x tree130_jets.C
/// ~~~
///
/// \macro_code
///
/// \author Rene Brun

#ifdef JETS_SECOND_RUN

#include "TFile.h"
#include "TTree.h"
#include "TRandom.h"
#include "TROOT.h"
#include "TSystem.h"
#include "JetEvent.h"
#include "Riostream.h"


void write(Int_t nev=100)
{
   //write nev Jet events
   TFile f("JetEvent.root","recreate");
   auto T = new TTree("T", "Event example with Jets");
   auto event = new JetEvent;
   T->Branch("event", "JetEvent", &event, 8000, 2);

   for (Int_t ev=0; ev<nev; ev++) {
      event->Build();
      T->Fill();
   }

   T->Print();
   T->Write();
}

void read()
{
   //read the JetEvent file
   TFile f("JetEvent.root");
   auto T = f.Get<TTree>("T");
   JetEvent *event = 0;
   T->SetBranchAddress("event", &event);
   Long64_t nentries = T->GetEntries();

   for (Long64_t ev=0;ev<nentries;ev++) {
      T->GetEntry(ev);
      if (ev)
         continue; //dump first event only
      std::cout << " Event: "<< ev
                << "  Jets: " << event->GetNjet()
                << "  Tracks: " << event->GetNtrack()
                << "  Hits A: " << event->GetNhitA()
                << "  Hits B: " << event->GetNhitB() << std::endl;
   }
}

void pileup(Int_t nev=200)
{
   //make nev pileup events, each build with LOOPMAX events selected
   //randomly among the nentries
   TFile f("JetEvent.root");
   auto T = f.Get<TTree>("T");
   // Long64_t nentries = T->GetEntries();

   const Int_t LOOPMAX=10;
   JetEvent *events[LOOPMAX];
   Int_t loop;
   for (loop=0; loop<LOOPMAX; loop++)
      events[loop] = 0;
   for (Long64_t ev=0; ev<nev; ev++) {
      if (ev%10 == 0)
         printf("building pileup: %lld\n", ev);
      for (loop=0; loop<LOOPMAX; loop++) {
         Int_t rev = gRandom->Uniform(LOOPMAX);
         T->SetBranchAddress("event", &events[loop]);
         T->GetEntry(rev);
      }
  }
}

void jets(Int_t nev=100, Int_t npileup=200, Bool_t secondrun = true)
{
   // Embedding these loads inside the first run of the script is not yet
   // supported in v6
   // gROOT->ProcessLine(".L $ROOTSYS/tutorials/io/tree/JetEvent.cxx+");
   write(nev);
   read();
   pileup(npileup);
}

#else

//void jets(Int_t nev=100, Int_t npileup=200, Bool_t secondrun);
void tree130_jets(Int_t nev = 100, Int_t npileup = 200)
{
   TString tutdir = gROOT->GetTutorialDir();
   gROOT->ProcessLine(".L " + tutdir + "/io/tree/JetEvent.cxx+");
   gROOT->ProcessLine("#define JETS_SECOND_RUN yes");
   gROOT->ProcessLine("#include \"" __FILE__ "\"");
   gROOT->ProcessLine("jets(100, 200, true)");
}

#endif
