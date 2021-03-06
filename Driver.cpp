/*
 *  $Id: sample.cpp
 *  hog2
 *
 *  Created by Nathan Sturtevant on 5/31/05.
 *  Extended by Reza Mashayekhi in 2022.
 *
 * This file is part of HOG2. See https://github.com/nathansttt/hog2 for licensing information.
 *
 */

#include "Common.h"
#include "Driver.h"
#include "Map2DEnvironment.h"
#include "TemplateAStar.h"
#include "TextOverlay.h"
#include "MapOverlay.h"
#include <string>
#include <sstream>
#include <fstream>
#include <numeric>
#include "MapGenerators.h"
#include "ScenarioLoader.h"


using namespace std;

enum mode {
    kAddDH = 0,
    kIdentifyLowHeuristic = 1,
    kIdentifyHighHeuristic = 2,
    kFindPath = 3,
    kMeasureHeuristic = 4
};

enum mapType {
    kRandomMap10 = 0,
    kRandomMap20 = 1,
    kRoomMap8 = 2,
    kRoomMap16 = 3,
    kMazeMap1 = 4,
    kMazeMap2 = 5,
    kDAMap = 6,
    kSimpleMap = 7,
    kDA2Maps = 8
};

enum heuristicVersion {
    // F: First dimension  S: Second dimension  f: furthest  he: heuriistic error  ahe: all heuristics so far
    kFfSf = 0,
    kFaheSf = 1,
    kFaheShe = 2,
    kFaheSahe = 3,
    kSubFheShe = 4,
    kSubFaheShe = 5,
    kSubFheSheThe = 6,
    kSubFheSheTheFhe = 7,
    kSub5 = 8,
    
    kFM9DH = 9,
    kFM9DH_Fahe = 10,
    kFM4DHDH5 = 11,
    kFM4DHDH5_Fahe = 12,
    kDH5FM4DH = 13,
    kDH5FM4DH_Fahe = 14,
    k5FM2 = 15,
    k5FM2_Fahe = 16,
    k5FM2MED = 17
    
};

enum pivotsVersion{
    kR = 0,
    kO = 1
};

std::vector<xyLoc> path;

graphState nodeToDraw = -1;
xyLoc stateToDraw;
xyLoc start, goal;
GraphEnvironment *ge=0, *basege=0;
Graph *g=0, *base=0;
MapEnvironment *me = 0;
Map *map=0;
ScenarioLoader *sl;
std::vector<int> xs;
std::vector<int> ys;
std::vector<int> largestPartNodeNumbers;
int iindex[10];
int stop=0;

void LoadMap(Map *m);
void DoOneDimension(int label, double (*f)(double, double, double));
void NormalizeGraph();
void PrintGraph(Map *m, Graph *g);
void LoadSimpleMap(Map *m);
void LoadMaps(Map *m);
double ComputeResidual(Graph *g);
double OE(double dai, double dib, double dab);
double DH(double dai, double dib, double dab);
void DoDimensions(int startlabel, int nofd, int which);
void DoDH(int startlabel, int nofp);
void DoGDH(int startlabel, int nofp, short nofcp, short nofSamples);
int Median(int a[], int n);
void SaveSVG();
void DoDifferentHeuristics();
void DoDifferentDimensions();
void Do2Embeddings();
void CreateGraph();
void CreateGraph4D();

double ComputeHeuristic(std::vector<graphState> samples, Heuristic<graphState> *h);
double* ComputeConfidenceInterval(std::vector<int> a);
void ResetEdgeWeights(int label);
void StoreEdgeWeights(int label);
void StoreMapLocInNodeLabels();
void CreateConfidenceInterval();
void ComputeCapturedHeuristicAtEachDimension();
void FindLargestPart();

bool recording = false;
bool running = false;
bool mapChange = true;
bool graphChanged = true;

const float lerpspeed = 0.001;
float lerp = -lerpspeed;
bool doLerp = false;
char scenfile[1024];
char mapName[30];
char saveDirectory[1024];

template <class state>
class EmbeddingHeuristic : public Heuristic<state> {
public:
    EmbeddingHeuristic(Graph *graph, int startlabel, int numberofdimensions)
    { g = graph; label=startlabel; nofd = numberofdimensions; }
    Graph *GetGraph() { return g; }
    double HCost(const state &state1, const state &state2) const
    {
        double h=0;
        for(int i=0;i<nofd;i++){
            h+=fabs(g->GetNode(state1)->GetLabelF(label+i) - g->GetNode(state2)->GetLabelF(label+i));
        }
        return h;
        
    }
private:
    Graph *g;
    int label;
    int nofd;
};

template <class state>
class DifferentialHeuristic : public Heuristic<state> {
public:
    DifferentialHeuristic(Graph *graph, int beginninglabel, int numberofpivots)
    { g = graph; label = beginninglabel; nofp = numberofpivots; }
    Graph *GetGraph() { return g; }
    double HCost(const state &state1, const state &state2) const
    {
        double h=0;
        for(int i=0;i<nofp;i++){
            h=max(h, fabs(g->GetNode(state1)->GetLabelF(label+i)- g->GetNode(state2)->GetLabelF(label+i)));
        }
        return h;
    }
private:
    Graph *g;
    int label;
    int nofp;
    
};

template <class state>
class GraphMapHeuristicE : public Heuristic<state> {
public:
    GraphMapHeuristicE(Map *map, Graph *graph)
    :m(map), g(graph) {}
    Graph *GetGraph() { return g; }
    double HCost(const state &state1, const state &state2) const
    {
        int x1 = g->GetNode(state1)->GetLabelL(GraphSearchConstants::kMapX);
        int y1 = g->GetNode(state1)->GetLabelL(GraphSearchConstants::kMapY);
        int x2 = g->GetNode(state2)->GetLabelL(GraphSearchConstants::kMapX);
        int y2 = g->GetNode(state2)->GetLabelL(GraphSearchConstants::kMapY);

        double a = ((x1>x2)?(x1-x2):(x2-x1));
        double b = ((y1>y2)?(y1-y2):(y2-y1));
        //TODO; Replace ROOT_TWo
        return (a>b)?(b*ROOT_TWO+a-b):(a*ROOT_TWO+b-a);
    }
private:
    Map *m;
    Graph *g;
};

template <class state>
class ZeroHeuristicE : public Heuristic<state> {
public:
    ZeroHeuristicE(Map *map, Graph *graph)
    :m(map), g(graph) {}
    Graph *GetGraph() { return g; }
    double HCost(const state &state1, const state &state2) const
    {
        return 0;
    }
private:
    Map *m;
    Graph *g;
};


template <class state>
class GraphHeuristicContainerE : public Heuristic<state> {
public:
    GraphHeuristicContainerE(Graph *gg) {g = gg;}
    ~GraphHeuristicContainerE() {}
    virtual Graph *GetGraph() { return g; }
    void AddHeuristic(Heuristic<state> *h) { heuristics.push_back(h); }
    void RemoveHeuristic() { heuristics.pop_back(); }
    int Size() { return heuristics.size(); }
    Heuristic<state> * GetIthHeuristic(int i) { return heuristics[i];}
    virtual double HCost(const state &state1, const state &state2) const
    {
        double cost = 0;
        for (unsigned int x = 0; x < heuristics.size(); x++)
            cost = max(cost, heuristics[x]->HCost(state1, state2));
        return cost;
    }
    
private:
    std::vector<Heuristic<state>*> heuristics;
    Graph *g;
};

GraphHeuristicContainerE <graphState>* DoMultipleFMDH(int startlabel, int nofmd, heuristicVersion hv, int nofCanPiv = 50, int nofSamples = 400);
GraphHeuristicContainerE <graphState>* DoMultipleFMDHI(int label, int nofmd, short nofcp ,short nofSamples);

GraphHeuristicContainerE <graphState>* DoLineH(int startlabel, int nofmd, heuristicVersion hv, int nofCanPiv = 50, int nofSamples = 400);

int main(int argc, char* argv[])
{
    InstallHandlers();
    RunHOGGUI(argc, argv, 1200, 1200);
    return 0;
}

/**
 * Allows you to install any keyboard handlers needed for program interaction.
 */
void InstallHandlers()
{
    InstallKeyboardHandler(MyDisplayHandler, "Load Map", "Load map", kAnyModifier, '0', '8');

    InstallKeyboardHandler(MyDisplayHandler, "Lerp", "restart lerp", kAnyModifier, 'l');
    InstallKeyboardHandler(MyDisplayHandler, "Record", "Record a movie", kAnyModifier, 'r');
    InstallKeyboardHandler(MyDisplayHandler, "Help", "Draw help", kAnyModifier, '?');
    InstallKeyboardHandler(MyDisplayHandler, "Clear", "Clear DH", kAnyModifier, '|');
    InstallKeyboardHandler(MyDisplayHandler, "Speed Up", "Increase speed of A* search", kAnyModifier, ']');
    InstallKeyboardHandler(MyDisplayHandler, "Slow Down", "Decrease speed of A* search", kAnyModifier, '[');
    InstallKeyboardHandler(MyDisplayHandler, "Add DH", "Switch to mode for add/study DH placement", kAnyModifier, 'a');
    InstallKeyboardHandler(MyDisplayHandler, "Show DH", "Toggle drawing the DH", kAnyModifier, 'd');
    InstallKeyboardHandler(MyDisplayHandler, "Measure", "Measure Heuristic", kAnyModifier, 'm');
    InstallKeyboardHandler(MyDisplayHandler, "Path", "Find path using current DH", kAnyModifier, 'p');
    InstallKeyboardHandler(MyDisplayHandler, "Test High", "Test to find state pairs with high heuristic value", kAnyModifier, 'h');
    InstallKeyboardHandler(MyDisplayHandler, "Test Low", "Test to find state pairs with low heuristic value", kAnyModifier, 'l');

    InstallCommandLineHandler(MyCLHandler, "-map", "-map filename", "Selects the default map to be loaded.");
        
    InstallWindowHandler(MyWindowHandler);

    InstallMouseClickHandler(MyClickHandler);
}

void CreateMap(mapType which)
{
    if (map)
        delete map;
    if (me)
        delete me;
    if (base)
        delete base;
    if (basege)
        delete basege;
    if (g)
        delete g;
    if (ge)
        delete ge;

    nodeToDraw = -1;
    
    static int seed = 20;
    srandom(seed++);
    bool loadedmap=false;
    
    if(gDefaultMap[0] != 0){
        map = new Map(gDefaultMap);
        //map = new Map(80,80);
        //MakeRandomMap(map, 0);
        //gDefaultMap[0]=0;
        loadedmap=true;
        sl = new ScenarioLoader(scenfile);
    }
    else{
        map = new Map(80,80);
        switch (which)
        {
            case kRandomMap20:
                MakeRandomMap(map, 20);
                break;
            case kRandomMap10:
                MakeRandomMap(map, 10);
                break;
            case kRoomMap8:
                BuildRandomRoomMap(map, 8);
                break;
            case kRoomMap16:
                BuildRandomRoomMap(map, 16);
                break;
            case kMazeMap1:
                MakeMaze(map, 1);
                break;
            case kMazeMap2:
                MakeMaze(map, 2);
                break;
            case kDAMap:
                LoadMap(map);
                break;
            case kSimpleMap:
                LoadSimpleMap(map);
                break;
            case kDA2Maps:
                LoadMaps(map);
                break;
        }
    }
    me = new MapEnvironment(map);
    if (doLerp)
    {
        base = GraphSearchConstants::GetUndirectedGraph(map);
        basege = new GraphEnvironment(base);
        ge->SetDirected(false);
    }
    if(loadedmap==true){
        //CreateGraph4D();
        g = GraphSearchConstants::GetUndirectedGraph(map);
        ge = new GraphEnvironment(g);
        ge->SetDirected(false);
        
        StoreEdgeWeights(kEdgeWeight+1);
        StoreMapLocInNodeLabels();
        FindLargestPart();
        //DoDifferentHeuristics();
        
        //ComputeCapturedHeuristicAtEachDimension();
        //CreateConfidenceInterval();
        //DoDifferentDimensions();
        //Do2Embeddings();
    }
    NormalizeGraph();
    
    ge->SetDrawEdgeCosts(false);
    ge->SetColor(Colors::white);
    if (doLerp)
    {
        basege->SetDrawEdgeCosts(false);
        basege->SetColor(Colors::white);
    }
    mapChange = true;
    graphChanged = true;
    SaveSVG();
    //exit(0);
}


void DoDifferentHeuristics(){
    int nofp=10;
    int nofd=10;
    int nofmd=5;
    short nofcp=7;
    short nofSamples=400;
    
    /// FMn
    /*
    DoDimensions(GraphSearchConstants::kFirstData, nofd, 0);
    GraphMapHeuristicE<graphState> h00(map, g);
    EmbeddingHeuristic<graphState> h01(g, GraphSearchConstants::kFirstData, nofd);
    GraphHeuristicContainerE <graphState> h0(g);
    h0.AddHeuristic(&h00);
    h0.AddHeuristic(&h01);
    */
    
    /// nDH
    /*
    DoDH(GraphSearchConstants::kFirstData + nofd, nofp);
    DifferentialHeuristic<graphState> h10(g, GraphSearchConstants::kFirstData + nofd, nofp);
    GraphHeuristicContainerE <graphState> h1(g);
    // delete this when
    GraphMapHeuristicE<graphState> h00(map, g);
    h1.AddHeuristic(&h00);
    h1.AddHeuristic(&h10);
    */
    
    /// Max (FMn/2, n/2DH)
    /*
    EmbeddingHeuristic<graphState> h21(g, GraphSearchConstants::kFirstData, nofd / 2);
    DifferentialHeuristic<graphState> h22(g, GraphSearchConstants::kFirstData + nofd, nofp / 2);
    /// GraphMapHeuristicE<graphState> z2(map,g);
    GraphHeuristicContainerE <graphState> h2(g);
    h2.AddHeuristic(&h00);
    h2.AddHeuristic(&h21);
    h2.AddHeuristic(&h22);
    */
    
    /// (n-1)FastMap + DH
    /*
    DoDimensions(GraphSearchConstants::kFirstData+nofp+nofd, nofd, 1);
    EmbeddingHeuristic<graphState> h30(g, GraphSearchConstants::kFirstData+nofp+nofd, nofd);
    GraphHeuristicContainerE <graphState> h3(g);
    h3.AddHeuristic(&h00);
    h3.AddHeuristic(&h30);
    */
    
    /// Max (FM(n/2-1)+DH, n/2DH)
    ///
    /*
    DoDimensions(GraphSearchConstants::kFirstData+nofp+2*nofd, nofd/2, 1);
    EmbeddingHeuristic<graphState> h41(g, GraphSearchConstants::kFirstData + nofp + 2 * nofd, nofd / 2);
    DifferentialHeuristic<graphState> h42(g, GraphSearchConstants::kFirstData + nofd, nofp / 2);
    //GraphMapHeuristicE<graphState> z2(map,g);
    GraphHeuristicContainerE <graphState> h4(g);
    h4.AddHeuristic(&h00);
    h4.AddHeuristic(&h41);
    h4.AddHeuristic(&h42);
    */
    
    /// Max n/2(FM + DH)
    /*
    GraphHeuristicContainerE <graphState>* h5 = DoMultipleFMDH(GraphSearchConstants::kFirstData+nofp+3*nofd, nofmd, kFaheSahe);
    
    GraphHeuristicContainerE <graphState>* h6 = DoMultipleFMDH(GraphSearchConstants::kFirstData+nofp+4*nofd, nofmd, kFM9DH);
    
    GraphHeuristicContainerE <graphState>* h7 = DoMultipleFMDH(GraphSearchConstants::kFirstData+nofp+5*nofd, nofmd, kFM4DHDH5);
    
    GraphHeuristicContainerE <graphState>* h8 = DoMultipleFMDH(GraphSearchConstants::kFirstData+nofp+6*nofd, nofmd, kDH5FM4DH);
    
    GraphHeuristicContainerE <graphState>* h900 = DoMultipleFMDH(GraphSearchConstants::kFirstData+nofp+7*nofd, nofmd, k5FM2);
    
    //GraphHeuristicContainerE <graphState>* h910 = DoMultipleFMDH(GraphSearchConstants::kFirstData+nofp+8*nofd, nofmd, k5FM2MED);
    
    GraphHeuristicContainerE <graphState>* h911 = DoMultipleFMDH(GraphSearchConstants::kFirstData+nofp+9*nofd, nofmd, kFaheSf);
    
    GraphHeuristicContainerE <graphState>* h912 = DoMultipleFMDH(GraphSearchConstants::kFirstData+nofp+10*nofd, nofmd, kFM9DH_Fahe);
    */
    GraphHeuristicContainerE <graphState>* h913 = DoMultipleFMDH(GraphSearchConstants::kFirstData+nofp+11*nofd, nofmd, kFM4DHDH5_Fahe);
    
    GraphHeuristicContainerE <graphState>* h914 = DoMultipleFMDH(GraphSearchConstants::kFirstData+nofp+12*nofd, nofmd, kDH5FM4DH_Fahe);
    
    //GraphHeuristicContainerE <graphState>* h915 = DoMultipleFMDH(GraphSearchConstants::kFirstData+nofp+13*nofd, nofmd, k5FM2_Fahe);
    
    
    
    /// Subset DH
    /*
    DoGDH(GraphSearchConstants::kFirstData + nofp + 4 * nofd, nofp, 100, 400);
    DifferentialHeuristic<graphState> h60(g, GraphSearchConstants::kFirstData + nofp + 4 * nofd, nofp);
    GraphHeuristicContainerE <graphState> h6(g);
    h6.AddHeuristic(&h00);
    h6.AddHeuristic(&h60);
    */
    
    /// subset multiple FMDH
    //GraphHeuristicContainerE <graphState>* h7 = DoMultipleFMDH(GraphSearchConstants::kFirstData + nofp + 5 * nofd, nofmd, kSubFheShe, 50 ,400);
    
    /*
    DoDimensions(GraphSearchConstants::kFirstData+nofp + 4*nofd + 50*2, nofd, 2);
    EmbeddingHeuristic<graphState> h70(g, GraphSearchConstants::kFirstData + nofp + 4*nofd + 50*2, nofd);
    GraphHeuristicContainerE <graphState> h7(g);
    h7.AddHeuristic(&h00);
    h7.AddHeuristic(&h70);
     
    //GraphHeuristicContainerE <graphState>* h7 = DoLineH(GraphSearchConstants::kFirstData+nofp + 4*nofd + 50*2, nofmd, kSubFheShe, 50 ,400);
    */
    
    /// sub 3 fm2+DH, DH
    //GraphHeuristicContainerE <graphState>* h8 = DoMultipleFMDH(GraphSearchConstants::kFirstData + nofp + 5 * nofd + 100, nofmd-2, kSubFheSheThe, 50 ,400);
    
    
    //GraphHeuristicContainerE <graphState>* h7 = DoMultipleFMDH(GraphSearchConstants::kFirstData+nofp + 4*nofd + 50*2, nofmd-3, kSubFheSheTheFhe, 50 ,400);
    //GraphHeuristicContainerE <graphState>* h7 = DoMultipleFMDH(GraphSearchConstants::kFirstData+nofp + 4*nofd + 50*2, nofmd-2, kSubFheSheThe, 50 ,400);
    //GraphHeuristicContainerE <graphState>* h7 = DoMultipleFMDH(GraphSearchConstants::kFirstData+nofp + 4*nofd + 51*2, nofmd, kSubFaheShe, 50 ,400);
    
    
    
    
    //DoGDH(GraphSearchConstants::kFirstData+nofp+2*nofd, nofp, nofcp, nofSamples);
    
    //Graph *g2 = GraphSearchConstants::GetUndirectedGraph(map);
    //DoDimensions(g2, GraphSearchConstants::kFirstData+nofp, nofd, 2);
    //NormalizeGraph();
    
    /*DifferentialHeuristic<graphState> h40(g, GraphSearchConstants::kFirstData+nofp+2*nofd, nofp);
    GraphHeuristicContainerE <graphState> h4(g);
    h4.AddHeuristic(&h00);
    h4.AddHeuristic(&h40);*/
    
    //GraphHeuristicContainerE <graphState>* h6 = DoMultipleFMDHI(440, nofmd, nofcp, nofSamples);
    
    //EmbeddingHeuristic<graphState> h4(g2, GraphSearchConstants::kFirstData+nofp, nofd);
    
    int nofh = 2;
    std::vector<FILE*> files;
    cout<<"doing the search"<<endl;
    
    for(int i=0;i<nofh;i++){
        std::string fname = saveDirectory + std::string("/Different_Heuristics/NoE/");
        FILE *f = fopen((fname+mapName+" - h"+ to_string(i) +".txt").c_str(), "w+");
        files.push_back(f);
    }

    ResetEdgeWeights(kEdgeWeight+1);
    std::vector<Heuristic<graphState>*> heuristics;
    //heuristics.push_back(&h0);
    //heuristics.push_back(&h1); heuristics.push_back(&h2);
    //heuristics.push_back(&h3); heuristics.push_back(&h4);
    //heuristics.push_back(&h4);
    //heuristics.push_back(h5);
    //heuristics.push_back(h6); heuristics.push_back(h7); heuristics.push_back(h8);
    //heuristics.push_back(h900); //heuristics.push_back(h92);
    //heuristics.push_back(h911); heuristics.push_back(h912); heuristics.push_back(h913);
    //heuristics.push_back(h914); heuristics.push_back(h915);
    heuristics.push_back(h913);
    heuristics.push_back(h914);
    
    TemplateAStar<graphState, graphMove, GraphEnvironment> astar;
    std::vector<graphState> p;
    ZeroHeuristic<graphState> z;
    astar.InitializeSearch(ge, 0 , 1, p);
    //astar.SetHeuristic(&h1);
    
    // Calculate corect length Path
    /*
    cout<<"calculateing correct length paths"<<endl;
    srandom(68949123);
    std::vector<double> pL;
    for(int j=0;j<sl->GetNumExperiments();j++){
        node* startn = g->GetNode(largestPartNodeNumbers[random() % largestPartNodeNumbers.size()]);
        node* goaln = g->GetNode(largestPartNodeNumbers[random() % largestPartNodeNumbers.size()]);
        if(startn->GetLabelF(GraphSearchConstants::kXCoordinate - 1) == 1 &&
           goaln->GetLabelF(GraphSearchConstants::kXCoordinate - 1) == 1){
            astar.SetHeuristic(&z);
            astar.GetPath(ge, startn->GetNum(), goaln->GetNum(), p);
            pL.push_back(ge->GetPathLength(p));
        }
    }
    */
    
    
    //cout<<h6->HCost(15, 1345)<<endl;
    for(int i=0;i<nofh;i++){
        int ne=0;
        std::vector<int> expanded;
        // it should be srandom for random problems. here is srand to be consistent with previous results
        srandom(68949123);
        
        for(int j=0;j<sl->GetNumExperiments();j++){
            //int j = rand() % sl->GetNumExperiments();
            Experiment e = sl->GetNthExperiment(j);
            xyLoc start, goal;
            start.x = e.GetStartX();
            start.y = e.GetStartY();
            goal.x = e.GetGoalX();
            goal.y = e.GetGoalY();
            
            
            //node* startn = g->GetNode(largestPartNodeNumbers[random() % largestPartNodeNumbers.size()]);
            //node* goaln = g->GetNode(largestPartNodeNumbers[random() % largestPartNodeNumbers.size()]);
            
            
            node* startn = g->GetNode(map->GetNodeNum(start.x, start.y));
            node* goaln = g->GetNode(map->GetNodeNum(goal.x, goal.y));
            
            
            //cout<<startn->GetNum()<<" ";
            // If it was in the largest part
            if(startn->GetLabelF(GraphSearchConstants::kXCoordinate - 1) == 1 &&
               goaln->GetLabelF(GraphSearchConstants::kXCoordinate - 1) == 1){
                astar.SetHeuristic(heuristics[i]);
                //cout<<"1"<<endl;
                astar.GetPath(ge, startn->GetNum(), goaln->GetNum(), p);
                //cout<<"2"<<endl;
                //astar.GetPath(gge,startn,goaln,p);
                int ex = astar.GetNodesExpanded();
                expanded.push_back(ex);
                ne+=ex;
                
                // Rnadom problems path length checking
                /*
                if (abs(ge->GetPathLength(p) - pL[j])>0.01){
                    cout<<"Not correct Length"<<endl;
                    cout<<"Start: "<<ge->GetLocation(startn->GetNum()).x<<" "<< ge->GetLocation(startn->GetNum()).y<<" Goal:"<<ge->GetLocation(goaln->GetNum()).x<<" "<<ge->GetLocation(goaln->GetNum()).y<<endl;
                    cout<<pL[j]<<" "<<ge->GetPathLength(p)<<endl;
                    exit(0);
                }
                */
                
                // Scenario path length checking
                
                if (abs( ge->GetPathLength(p) - e.GetDistance())>0.01){
                    cout<<"Not correct Length"<<endl;
                    cout<<"Start: "<<start.x<<" "<< start.y<<" Goal:"<<goal.x<<" "<<goal.y<<endl;
                    exit(0);
                }
                
                
                
                //cout<<j<<endl;
                //cout<<gge->GetPathLength(p)<<endl;
                //cout<<astar.GetNodesExpanded()<<" ";
            }
        }
        cout<<"average nodes expanded by h"<<i<<": "<<ne/sl->GetNumExperiments()<<endl;
        //cout<<"? wins: "<<expanded1.size()<<endl;
        int a [expanded.size()];
        for(int j=0; j < expanded.size(); j++)
        {
            a[j]=expanded[j];
        //    cout<< expanded[i]<< " ";
        }
        sort(a, a+expanded.size());
        for(int j=0; j < expanded.size(); j++)
        {
            //cout<< a[j]<< " ";
            string s=to_string(a[j])+"\n";
            fputs(s.c_str(), files[i]);
            fflush(files[i]);
        }
        
        cout<<"\n Median of ?? in ? is: "<<Median(a,expanded.size());
        cout<<endl;
        
        //cout<<start.x<<" "<<start.y<<" "<<goal.x<<"  "<<goal.y<<endl;
        //cout<< z.HCost(7,14);
        //printf("\n");
        //cout<<p.size()<<" ";
        //for(int i=0; i<p.size(); ++i)
          //    cout << p[i] << ' ';
        //cout<<endl;
        //cout<< astar.GetNodesExpanded()<<endl;
        //cout<<gg->GetNode(0)->GetLabelL(GraphSearchConstants::kMapY)<<endl;
        //cout<<map->GetNodeNum(start.x, start.y)<<" "<<map->GetNodeNum(goal.x, goal.y);
        fclose(files[i]);
        //exit(0);
    }
    
    // Finding the Worst problem
    /*
    cout<<"finding worst problem"<<endl;
    int m=0;
    std::vector<int> index;
    for(int j=0;j<sl->GetNumExperiments();j++){
            Experiment e = sl->GetNthExperiment(j);
            xyLoc start, goal;
            start.x = e.GetStartX();
            start.y = e.GetStartY();
            goal.x = e.GetGoalX();
            goal.y = e.GetGoalY();
            astar.SetHeuristic(heuristics[1]);
            astar.GetPath(ge,map->GetNodeNum(start.x, start.y),map->GetNodeNum(goal.x, goal.y),p);
            int ex1 = astar.GetNodesExpanded();
            astar.SetHeuristic(heuristics[4]);
            astar.GetPath(ge,map->GetNodeNum(start.x, start.y),map->GetNodeNum(goal.x, goal.y),p);
            int ex4 = astar.GetNodesExpanded();
            if(ex4-ex1>m){
                m=ex4-ex1;
                index.push_back(j);
            }
    }
    cout<<" m is" <<m<<endl;
    Experiment e = sl->GetNthExperiment(index[index.size()-3]);
    graphState t;
    xyLoc start, goal;
    start.x = e.GetStartX();
    start.y = e.GetStartY();
    goal.x = e.GetGoalX();
    goal.y = e.GetGoalY();
    //astar.SetStopAfterGoal(true);
    astar.InitializeSearch(ge, map->GetNodeNum(start.x, start.y),map->GetNodeNum(goal.x, goal.y), p);
    cout<<start.x<<" "<<start.y<<" "<<goal.x<< " "<< goal.y<<endl;
    astar.SetHeuristic(&h4);
    //printf("\n %d \n", n->GetNum());
    while (astar.GetNumOpenItems() > 0)
    {
        //cout<<"s"<<endl;
        t = astar.GetOpenItem(0).data;
        //xs.push_back(ge->GetLocation(t).x);
        //ys.push_back(ge->GetLocation(t).y);
        if (t==map->GetNodeNum(goal.x, goal.y))
            break;
        astar.DoSingleSearchStep(p);
    }
    //cout<<"xs size "<<xs.size()<<endl;
    */
    
    ge->SetDrawEdgeCosts(false);
    ge->SetColor(Colors::white);
    if (doLerp)
    {
        basege->SetDrawEdgeCosts(false);
        basege->SetColor(Colors::white);
    }
    mapChange = true;
    graphChanged = true;
}

void DoDifferentDimensions(){
    ResetEdgeWeights(kEdgeWeight+1);
    
    int nofp=10;
    DoDH(GraphSearchConstants::kFirstData, nofp);
    int nofd=10;
    DoDimensions(GraphSearchConstants::kFirstData+nofp, nofd, 0);
    
    //NormalizeGraph();
    int nofh = 10;
    
    
    cout<<"doing the search"<<endl;
    std::vector<FILE*> files;
    std::vector<Heuristic<graphState>*> heuristics;
    
    for(int i=0;i<nofh;i++){
        std::string fname = "/home/rezamshy/Outputs/Different_Dimensions/NoE/";
        FILE *f = fopen((fname+mapName+" - dh"+ to_string(i+1) +".txt").c_str(), "w+");
        files.push_back(f);
        //EmbeddingHeuristic<graphState> h(g, GraphSearchConstants::kFirstData+nofp, i+1);
        //heuristics.push_back(&h);
    }

    TemplateAStar<graphState, graphMove, GraphEnvironment> astar;
    std::vector<graphState> p;
    //ZeroHeuristic<graphState> z;
    //EmbeddingHeuristic<graphState> h0(g, GraphSearchConstants::kFirstData+nofp, nofd);
    //DifferentialHeuristic<graphState> h1(g, GraphSearchConstants::kFirstData, nofp);
    //EmbeddingHeuristic<graphState> h21(g, GraphSearchConstants::kFirstData+nofp, nofd/2);
    //DifferentialHeuristic<graphState> h22(g, GraphSearchConstants::kFirstData, nofp/2);
    //GraphMapHeuristicE<graphState> z2(map,g);
    //GraphHeuristicContainerE <graphState> h2(g);
    //h2.AddHeuristic(&h21);
    //h2.AddHeuristic(&h22);
    //EmbeddingHeuristic<graphState> h3(ggg, GraphSearchConstants::kFirstData+nofp, nofd);
    
    //heuristics.push_back(&h0);heuristics.push_back(&h1);heuristics.push_back(&h2);
    //heuristics.push_back(&h3);
    
    
    astar.InitializeSearch(ge, 0 , 1, p);
    //astar.SetHeuristic(&h1);
    
    
    for(int i=0;i<nofh;i++){
        int ne=0;
        std::vector<int> expanded;
        DifferentialHeuristic<graphState> h(g, GraphSearchConstants::kFirstData, i+1);
        for(int j=0;j<sl->GetNumExperiments();j++){
            //int j = rand() % sl->GetNumExperiments();
            Experiment e = sl->GetNthExperiment(j);
            xyLoc start, goal;
            start.x = e.GetStartX();
            start.y = e.GetStartY();
            goal.x = e.GetGoalX();
            goal.y = e.GetGoalY();
            //node *n = g->GetRandomNode();
            //int startn = n->GetNum();
            //n = g->GetRandomNode();
            //int goaln = n->GetNum();
            node* startn = g->GetNode(map->GetNodeNum(start.x, start.y));
            node* goaln = g->GetNode(map->GetNodeNum(goal.x, goal.y));
            if(startn->GetLabelF(GraphSearchConstants::kXCoordinate - 1) == 1 &&
               goaln->GetLabelF(GraphSearchConstants::kXCoordinate - 1) == 1){
                astar.SetHeuristic(&h);
                astar.GetPath(ge, startn->GetNum(), goaln->GetNum(), p);
                //astar.GetPath(gge,startn,goaln,p);
                int ex = astar.GetNodesExpanded();
                expanded.push_back(ex);
                
                ne+=ex;
            }
            //cout<<gge->GetPathLength(p)<<endl;
            //cout<<astar.GetNodesExpanded()<<" ";
        }
        cout<<"average nodes expanded by dh"<<i+1<<": "<<ne/sl->GetNumExperiments()<<endl;
        //cout<<"? wins: "<<expanded1.size()<<endl;
        int a [expanded.size()];
        for(int j=0; j < expanded.size(); j++)
        {
            a[j]=expanded[j];
        //    cout<< expanded[i]<< " ";
        }
        sort(a, a+expanded.size());
        for(int j=0; j < expanded.size(); j++)
        {
            //cout<< a[j]<< " ";
            string s=to_string(a[j])+"\n";
            fputs(s.c_str(), files[i]);
        }
        
        cout<<"\n Median of ?? in ? is: "<<Median(a,expanded.size());
        cout<<endl;
        
        //cout<<start.x<<" "<<start.y<<" "<<goal.x<<"  "<<goal.y<<endl;
        //cout<< z.HCost(7,14);
        //printf("\n");
        //cout<<p.size()<<" ";
        //for(int i=0; i<p.size(); ++i)
          //    cout << p[i] << ' ';
        //cout<<endl;
        //cout<< astar.GetNodesExpanded()<<endl;
        //cout<<gg->GetNode(0)->GetLabelL(GraphSearchConstants::kMapY)<<endl;
        //cout<<map->GetNodeNum(start.x, start.y)<<" "<<map->GetNodeNum(goal.x, goal.y);
        fclose(files[i]);
        //exit(0);
    }
    
    
    //FM
    files.clear();
    for(int i=0;i<nofh;i++){
        std::string fname = "/home/rezamshy/Outputs/Different_Dimensions/NoE/";
        FILE *f = fopen((fname+mapName+" - FM"+ to_string(i+1) +".txt").c_str(), "w+");
        files.push_back(f);
    }
    cout<<"aaaaaaaaaaa"<<endl;
    //astar.InitializeSearch(gge, 0 , 1, p);
    cout<<"bbbbbbbbbbb"<<endl;
    for(int i=0;i<nofh;i++){
        int ne=0;
        std::vector<int> expanded;
        EmbeddingHeuristic<graphState> h(g, GraphSearchConstants::kFirstData+nofp, i+1);
        for(int j=0;j<sl->GetNumExperiments();j++){
            Experiment e = sl->GetNthExperiment(j);
            xyLoc start, goal;
            start.x = e.GetStartX();
            start.y = e.GetStartY();
            goal.x = e.GetGoalX();
            goal.y = e.GetGoalY();
            astar.SetHeuristic(&h);
            astar.GetPath(ge,map->GetNodeNum(start.x, start.y),map->GetNodeNum(goal.x, goal.y),p);
            int ex = astar.GetNodesExpanded();
            expanded.push_back(ex);
            ne+=ex;
        }
        cout<<"average nodes expanded by FM"<<i+1<<": "<<ne/sl->GetNumExperiments()<<endl;
        int a [expanded.size()];
        for(int j=0; j < expanded.size(); j++)
        {
            a[j]=expanded[j];
        }
        sort(a, a+expanded.size());
        for(int j=0; j < expanded.size(); j++)
        {
            //cout<< a[j]<< " ";
            string s=to_string(a[j])+"\n";
            fputs(s.c_str(), files[i]);
        }
        
        cout<<"\n Median of ?? in ? is: "<<Median(a,expanded.size());
        cout<<endl;
        
        fclose(files[i]);
    }
    
    
    //FM+DH
    files.clear();
    for(int i=1;i<nofh;i++){
        std::string fname = "/home/rezamshy/Outputs/Different_Dimensions/NoE/";
        FILE *f = fopen((fname+mapName+" - FM+DH"+ to_string(i+1) +".txt").c_str(), "w+");
        files.push_back(f);
    }
    //astar.InitializeSearch(gge, 0 , 1, p);
    for(int i=1;i<nofh;i++){
        int ne=0;
        std::vector<int> expanded;

        DoDimensions(GraphSearchConstants::kFirstData+nofp+nofd, i+1, 1);
        EmbeddingHeuristic<graphState> h(g, GraphSearchConstants::kFirstData+nofp+nofd, i+1);
        for(int j=0;j<sl->GetNumExperiments();j++){
            Experiment e = sl->GetNthExperiment(j);
            xyLoc start, goal;
            start.x = e.GetStartX();
            start.y = e.GetStartY();
            goal.x = e.GetGoalX();
            goal.y = e.GetGoalY();
            astar.SetHeuristic(&h);
            astar.GetPath(ge,map->GetNodeNum(start.x, start.y),map->GetNodeNum(goal.x, goal.y),p);
            int ex = astar.GetNodesExpanded();
            expanded.push_back(ex);
            ne+=ex;
            //cout<<j<<endl;
        }
        cout<<"average nodes expanded by FM+DH"<<i+1<<": "<<ne/sl->GetNumExperiments()<<endl;
        int a [expanded.size()];
        for(int j=0; j < expanded.size(); j++)
        {
            a[j]=expanded[j];
        }
        sort(a, a+expanded.size());
        for(int j=0; j < expanded.size(); j++)
        {
            //cout<< a[j]<< " ";
            string s=to_string(a[j])+"\n";
            fputs(s.c_str(), files[i-1]);
        }
        
        cout<<"\n Median of ?? in ? is: "<<Median(a,expanded.size());
        cout<<endl;
        
        fclose(files[i-1]);
    }
    
    mapChange = true;
    graphChanged = true;
}

void Do2Embeddings(){

    int nofp=2;
    DoDH(GraphSearchConstants::kFirstData, nofp);
    int nofd=2;
    DoDimensions(GraphSearchConstants::kFirstData+nofp, nofd, 0);
    
    DoDimensions(GraphSearchConstants::kFirstData+nofp+nofd, nofd, 1);
    int nofh = 3;
    std::vector<FILE*> files;
    cout<<"doing the search"<<endl;
    for(int i=0;i<nofh;i++){
        std::string fname = "/home/rezamshy/Outputs/2D/NoE/";
        FILE *f = fopen((fname+mapName+" - h"+ to_string(i) +".txt").c_str(), "w+");
        files.push_back(f);
    }
    TemplateAStar<graphState, graphMove, GraphEnvironment> astar;
    std::vector<graphState> p;
    EmbeddingHeuristic<graphState> h0(g, GraphSearchConstants::kFirstData+nofp, nofd);
    DifferentialHeuristic<graphState> h1(g, GraphSearchConstants::kFirstData, nofp);
    EmbeddingHeuristic<graphState> h2(g, GraphSearchConstants::kFirstData+nofp+nofd, nofd);
    std::vector<Heuristic<graphState>*> heuristics;
    heuristics.push_back(&h0);heuristics.push_back(&h1);heuristics.push_back(&h2);

    astar.InitializeSearch(ge, 0 , 1, p);
    for(int i=0;i<nofh;i++){
        int ne=0;
        std::vector<int> expanded;
        for(int j=0;j<sl->GetNumExperiments();j++){
            //int j = rand() % sl->GetNumExperiments();
            Experiment e = sl->GetNthExperiment(j);
            xyLoc start, goal;
            start.x = e.GetStartX();
            start.y = e.GetStartY();
            goal.x = e.GetGoalX();
            goal.y = e.GetGoalY();
            astar.SetHeuristic(heuristics[i]);
            astar.GetPath(ge,map->GetNodeNum(start.x, start.y),map->GetNodeNum(goal.x, goal.y),p);
            int ex = astar.GetNodesExpanded();
            expanded.push_back(ex);
            ne+=ex;
        }
        cout<<"average nodes expanded by h"<<i<<": "<<ne/sl->GetNumExperiments()<<endl;
        int a [expanded.size()];
        for(int j=0; j < expanded.size(); j++)
            a[j]=expanded[j];
        sort(a, a+expanded.size());
        for(int j=0; j < expanded.size(); j++)
        {
            string s=to_string(a[j])+"\n";
            fputs(s.c_str(), files[i]);
        }
        cout<<"\n Median of ?? in ? is: "<<Median(a,expanded.size());
        cout<<endl;
        fclose(files[i]);
    }
    mapChange = true;
    graphChanged = true;
}

void StoreEdgeWeights(int label){
     for (int x = 0; x < g->GetNumEdges(); x++)
    {
        edge *e = g->GetEdge(x);
        e->SetLabelF(label, e->GetWeight());
    }
}

void ResetEdgeWeights(int label){
    for (int x = 0; x < g->GetNumEdges(); x++)
    {
        edge *e = g->GetEdge(x);
        e->setWeight(e->GetLabelF(label));
    }
}

void StoreMapLocInNodeLabels(){
    for (int i=0;i<map->GetMapHeight(); i++){
        for(int j=0;j<map->GetMapWidth(); j++){
            if(map->GetNodeNum(j,i)!=-1){
                node *n = g->GetNode(map->GetNodeNum(j,i));
                n->SetLabelF(GraphSearchConstants::kXCoordinate, j);
                n->SetLabelF(GraphSearchConstants::kYCoordinate, i);
            }
        }
    }
}

int Median(int a[], int n)
{
    if(n%2==0)
        return (a[n/2-1]+a[n/2])/2;
    else
        return a[n/2];
}

double OE(double dai, double dib, double dab){
    return (dai + dab - dib)/2;
}

double DH(double dai, double dib, double dab){
    return dai;
}

double HDH(double dai, double dib, double dab){
    return 0.2*dai;
}

void DoDimensions(int startlabel, int nofd, int which)
{
    //only OE
    if (which==0){
        for(int i=0;i<nofd;i++){
            DoOneDimension(startlabel+i,OE);
        }
    }
    //FM Last DH
    if (which==1){
        for(int i=0;i<nofd-1;i++){
            DoOneDimension(startlabel+i,OE);
        }
        for(int i=nofd-1;i<nofd;i++){
            DoOneDimension(startlabel+i,DH);
        }
    }
    //first half OE, sencod half DH
    if (which==2){
        for(int i=0;i<nofd-1;i++){
            DoOneDimension(startlabel+i,HDH);
        }
        DoOneDimension(startlabel+nofd-1,DH);
    }
    
    //Multple fm+dh
    if (which==3){
        for(int i=0;i<nofd/2;i++){
            DoOneDimension(startlabel+i,OE);
        }
        for(int i=nofd/2;i<nofd-1;i++){
            DoOneDimension(startlabel+i,HDH);
        }
        DoOneDimension(startlabel+nofd-1,DH);
    }
    ResetEdgeWeights(kEdgeWeight+1);
}

void ResetSeenLabels(){
    for (int i = 0; i < g->GetNumNodes(); i++)
    {
        // Reseting them to not seen
        node *n = g->GetNode(i);
        n->SetLabelF(GraphSearchConstants::kXCoordinate - 1, 0);
    }
}
void DoOneDimension(int label, double (*f)(double,double,double))
{
    //ResetSeenLabels();
    printf("\n Dimension %d \n\n", label);
    TemplateAStar<graphState, graphMove, GraphEnvironment> astarf;
    TemplateAStar<graphState, graphMove, GraphEnvironment> astarb;
    std::vector<graphState> p;
    node *n = g->GetRandomNode();
    graphState s, m, t;
    ZeroHeuristic<graphState> z;
    //GraphLabelHeuristicE<graphState> z1(g);
    
    // Find a node in largest part
    while (n->GetLabelF(GraphSearchConstants::kXCoordinate - 1) != 1){
        n = g->GetRandomNode();
    }
    astarf.SetStopAfterGoal(false);
    astarf.InitializeSearch(ge, n->GetNum() , 0, p);
    astarf.SetHeuristic(&z);
    //printf("\n %d \n", n->GetNum());
    while (astarf.GetNumOpenItems() > 0)
    {
        t = astarf.GetOpenItem(0).data;
        astarf.DoSingleSearchStep(p);
    //    printf("%d ", t);
    }
    astarb.SetStopAfterGoal(false);
    astarb.InitializeSearch(ge, t, 0, p);
    astarb.SetHeuristic(&z);
    printf("\n");
    while (astarb.GetNumOpenItems() > 0)
    {
        s = astarb.GetOpenItem(0).data;
        astarb.DoSingleSearchStep(p);
    //    printf("%d ", s);
    }
    
    for(int i=0; i<16; i++)
    {
    //    printf("\n %d %f \n", astarf.GetItem(i).data, astarf.GetItem(i).f);
    }
    
    astarf.SetStopAfterGoal(false);
    //cout<<s<<" "<<t<<endl;
    astarf.InitializeSearch(ge, s, t, p);
    astarf.SetHeuristic(&z);
    while (astarf.GetNumOpenItems() > 0)
    {
        m = astarf.GetOpenItem(0).data;
        astarf.DoSingleSearchStep(p);
        //g->GetNode(m)->SetLabelF(GraphSearchConstants::kXCoordinate - 1, 1);
    }
    // assign first coordinates
    //(dai + dab ??? dib)/2
    double dab, dai, dib;
    astarf.GetClosedListGCost(t, dab);
    cout<<" s: "<<s<<" t: "<<t<<endl;
    printf(" dab %f \n", dab);
    //astarf.SetStopAfterGoal(true);
    //astarf.GetPath(ge,7,t,p);
    //astarf.SetStopAfterGoal(false);
    //astarf.ExtractPathToStartFromID(14,p);
    //cout<<p.size();
    //for(int i=0; i<p.size(); ++i)
      //    cout << p[i] << ' ';
          
    
    if (g->GetNumNodes()<20){
        PrintGraph(map, g);
        printf("\n\n\n");
    }
    
    
    
    /*for (int x = 0; x < g->GetNumNodes(); x++)
    {
        node *n = g->GetNode(x);
        astarf.GetClosedListGCost(x, dai);
        astarb.GetClosedListGCost(x, dib);
    //    printf("dai %f dib %f \n", dai, dib);
    //Change to constants
        //if(label == GraphSearchConstants::kFirstData){
            //n->SetLabelF(label, (dai + dab - dib)/2);
        n->SetLabelF(label, (*f)(dai, dib, dab ));
        if(x==2969)
            cout<<dai<<endl;
        //}
        //else{
            //n->SetLabelF(label, dai);
        //}
//        n->SetLabelF(label, dab*(dai/(dai+dib)));
    }*/
    
    for (int x = 0; x < g->GetNumNodes(); x++)
    {
        node *n = g->GetNode(x);
        if(n->GetLabelF(GraphSearchConstants::kXCoordinate - 1) == 1){
            astarf.GetClosedListGCost(x, dai);
            astarb.GetClosedListGCost(x, dib);
            n->SetLabelF(label, (*f)(dai, dib, dab ));
        }
        else
            n->SetLabelF(label, 0);
    }
    
    
    for (int x = 0; x < g->GetNumEdges(); x++)
    {
        edge *e = g->GetEdge(x);
        double w = fabs(g->GetNode(e->getFrom())->GetLabelF(label) - g->GetNode(e->getTo())->GetLabelF(label));
        //cout<<w<<" ";
        if ((e->GetWeight() - w) > 0)
            e->setWeight(e->GetWeight()-w);
        else if ((e->GetWeight() - w) > -0.0001)
            e->setWeight(0);
        else if ((e->GetWeight() - w) <= -0.0001)
        {
            /*if (w > 1.42){
                g->GetNode(e->getFrom())->SetLabelF(label, 0);
                g->GetNode(e->getTo())->SetLabelF(label, 0);
            }*/
            
            //cout<<g->GetNode(e->getFrom())->GetLabelF(label)<<" "<<g->GetNode(e->getTo())->GetLabelF(label)<<endl;
            cout<<"Negative Residual"<<endl;
            /*xs.push_back(ge->GetLocation(e->getFrom()).x);
            ys.push_back(ge->GetLocation(e->getFrom()).y);
            xs.push_back(ge->GetLocation(e->getTo()).x);
            ys.push_back(ge->GetLocation(e->getTo()).y);*/
            exit(0);
        }
        /*if (e->getFrom()==31896 && e->getTo()==31806){
            cout<<g->GetNode(e->getFrom())->GetLabelF(label)<<" "<<g->GetNode(e->getTo())->GetLabelF(label)<<endl;
            cout<<e->GetWeight()<<endl;
        }*/
    }
    if (g->GetNumNodes()<20){
        
        
        PrintGraph(map, g);
        printf("\n\n\n");
    }
    
    printf("\n residual:%f \n", ComputeResidual(g));
    //cout<< z1.HCost(7,14);
}

void DoOneDimension(int label, double (*f)(double,double,double), graphState s, graphState t){

    printf("\n Dimension %d \n\n", label);
    TemplateAStar<graphState, graphMove, GraphEnvironment> astarf;
    TemplateAStar<graphState, graphMove, GraphEnvironment> astarb;
    std::vector<graphState> p;
    ZeroHeuristic<graphState> z;

    astarb.SetStopAfterGoal(false);
    astarb.InitializeSearch(ge, t, s, p);
    astarb.SetHeuristic(&z);
    printf("\n");
    while (astarb.GetNumOpenItems() > 0)
        astarb.DoSingleSearchStep(p);
    
    astarf.SetStopAfterGoal(false);
    astarf.InitializeSearch(ge, s, t, p);
    astarf.SetHeuristic(&z);
    while (astarf.GetNumOpenItems() > 0)
        astarf.DoSingleSearchStep(p);
    
    double dab, dai, dib;
    astarf.GetClosedListGCost(t, dab);
    printf("\n dab %f \n", dab);
    
    for (int x = 0; x < g->GetNumNodes(); x++)
    {
        node *n = g->GetNode(x);
        astarf.GetClosedListGCost(x, dai);
        astarb.GetClosedListGCost(x, dib);
        n->SetLabelF(label, (*f)(dai, dib, dab ));
    }

    for (int x = 0; x < g->GetNumEdges(); x++)
    {
        edge *e = g->GetEdge(x);
        double w = fabs(g->GetNode(e->getFrom())->GetLabelF(label)-g->GetNode(e->getTo())->GetLabelF(label));
        //cout<<w<<" ";
        e->setWeight(e->GetWeight()-w);
    }

    printf("\n residual:%f \n", ComputeResidual(g));
}


void DoTwoDimensions (int label, double (*f)(double,double,double), graphState s){

    printf("\n Dimension %d \n\n", label);
    TemplateAStar<graphState, graphMove, GraphEnvironment> astarf;
    TemplateAStar<graphState, graphMove, GraphEnvironment> astarb;
    std::vector<graphState> p;
    ZeroHeuristic<graphState> z;
    graphState m,t;
    
    GraphMapHeuristicE<graphState> h0(map, g);
    GraphHeuristicContainerE <graphState> h(g);
    h.AddHeuristic(&h0);
    
    
    astarf.SetStopAfterGoal(false);
    astarf.InitializeSearch(ge, s, 0, p);
    astarf.SetHeuristic(&z);
    t=0;
    double max=0;
    double gc=0;
    while (astarf.GetNumOpenItems() > 0){
        m = astarf.GetOpenItem(0).data;
        astarf.DoSingleSearchStep(p);
        astarf.GetClosedListGCost(m, gc);
        
        if ((gc-h.HCost(s,m))>max ){
        
            max=gc-h.HCost(s,m);
            t=m;
        }
        
    }
        
    astarb.SetStopAfterGoal(false);
    astarb.InitializeSearch(ge, t, s, p);
    astarb.SetHeuristic(&z);
    printf("\n");
    while (astarb.GetNumOpenItems() > 0)
        astarb.DoSingleSearchStep(p);
    
    double dab, dai, dib;
    astarf.GetClosedListGCost(t, dab);
    cout<<"s "<<s<<" t "<<t<<endl;
    /*if(ge->GetLocation(s).x==41 && ge->GetLocation(s).y==19){
        
        xs[0]=ge->GetLocation(s).x;
        ys[0]=ge->GetLocation(s).y;
        xs[1]=ge->GetLocation(t).x;
        ys[1]=ge->GetLocation(t).y;
        xs[49]=xs[0];
        ys[49]=ys[0];
        cout<<"max is"<<max<<endl;
        int n1= map->GetNodeNum(127,48);
        astarf.GetClosedListGCost(n1, gc);
        cout<<"another "<<gc-h.HCost(s,n1)<<endl;
    }*/
    /*xs.push_back(ge->GetLocation(s).x);
    ys.push_back(ge->GetLocation(s).y);
    xs.push_back(ge->GetLocation(t).x);
    ys.push_back(ge->GetLocation(t).y);*/
    printf("\n dab %f \n", dab);
    
    for (int x = 0; x < g->GetNumNodes(); x++)
    {
        node *n = g->GetNode(x);
        astarf.GetClosedListGCost(x, dai);
        astarb.GetClosedListGCost(x, dib);
        n->SetLabelF(label, (*f)(dai, dib, dab ));
    }

    for (int x = 0; x < g->GetNumEdges(); x++)
    {
        edge *e = g->GetEdge(x);
        double w = fabs(g->GetNode(e->getFrom())->GetLabelF(label)-g->GetNode(e->getTo())->GetLabelF(label));
        //cout<<w<<" ";
        e->setWeight(e->GetWeight()-w);
    }
    printf("\n residual:%f \n", ComputeResidual(g));
    EmbeddingHeuristic<graphState> h1(g, label, 1);
    h.AddHeuristic(&h1);
    
    node *n = g->GetRandomNode();
    astarf.SetStopAfterGoal(false);
    astarf.InitializeSearch(ge, n->GetNum(), 0, p);
    astarf.SetHeuristic(&z);
    t=0;
    max=0;
    while (astarf.GetNumOpenItems() > 0){
        m = astarf.GetOpenItem(0).data;
        astarf.DoSingleSearchStep(p);
        astarf.GetClosedListGCost(m, gc);
        if ((gc-h.HCost(n->GetNum(),m))>max ){
            max=gc-h.HCost(n->GetNum(),m);
            t=m;
        }
        
    }
    
    astarb.SetStopAfterGoal(false);
    astarb.InitializeSearch(ge, t, 0, p);
    astarb.SetHeuristic(&z);
    printf("\n");
    s=0;
    max=0;
    while (astarb.GetNumOpenItems() > 0)
    {
        m = astarb.GetOpenItem(0).data;
        astarb.DoSingleSearchStep(p);
        astarb.GetClosedListGCost(m, gc);
        if (gc-h.HCost(t,m)>max ){
            max=gc-h.HCost(t,m);
            s=m;
        }
        
    }
    
    
    astarf.SetStopAfterGoal(false);
    astarf.InitializeSearch(ge, s, t, p);
    astarf.SetHeuristic(&z);
    while (astarf.GetNumOpenItems() > 0)
    {
        astarf.DoSingleSearchStep(p);
    }

    astarf.GetClosedListGCost(t, dab);
    cout<<"s "<<s<<" t "<<t<<endl;
    printf("\n dab %f \n", dab);

    for (int x = 0; x < g->GetNumNodes(); x++)
    {
        node *n = g->GetNode(x);
        astarf.GetClosedListGCost(x, dai);
        astarb.GetClosedListGCost(x, dib);
        n->SetLabelF(label+1, dai);
    }

    for (int x = 0; x < g->GetNumEdges(); x++)
    {
        edge *e = g->GetEdge(x);
        double w = fabs(g->GetNode(e->getFrom())->GetLabelF(label+1)-g->GetNode(e->getTo())->GetLabelF(label+1));
        e->setWeight(e->GetWeight()-w);
    }
    printf("\n residual:%f \n", ComputeResidual(g));
    
    
}

void DoOneDimension (int label, double (*f)(double,double,double), GraphHeuristicContainerE<graphState>* h, pivotsVersion pV, graphState ss = NULL, graphState tt = NULL){
    //ResetSeenLabels();
    printf("\n Dimension %d \n\n", label);
    TemplateAStar<graphState, graphMove, GraphEnvironment> astarf;
    TemplateAStar<graphState, graphMove, GraphEnvironment> astarb;
    std::vector<graphState> p;
    ZeroHeuristic<graphState> z;
    graphState s = ss, m, t = tt;
    node *n;
    double max, gc;
    
    // Pivots version
    if (pV == kO){
        StoreEdgeWeights(kEdgeWeight+2);
        ResetEdgeWeights(kEdgeWeight+1);
    }
    
    // Finding t
    if(tt == NULL){
        if (ss == NULL){
            n = g->GetRandomNode();
            // Find a node in largest part
            while (n->GetLabelF(GraphSearchConstants::kXCoordinate - 1) != 1){
                n = g->GetRandomNode();
            }
            s = n->GetNum();
        }
        astarf.SetStopAfterGoal(false);
        astarf.InitializeSearch(ge, s, 0, p);
        astarf.SetHeuristic(&z);
        t=0;
        max=0;
        gc=0;
        while (astarf.GetNumOpenItems() > 0){
            m = astarf.GetOpenItem(0).data;
            astarf.DoSingleSearchStep(p);
            astarf.GetClosedListGCost(m, gc);
            double hCost = h->HCost(s,m);
            if ((gc-hCost)>max){
                max=gc-hCost;
                t=m;
            }
        }
        // Finding s
        if (ss == NULL){
            astarb.SetStopAfterGoal(false);
            astarb.InitializeSearch(ge, t, 0, p);
            astarb.SetHeuristic(&z);
            printf("\n");
            s=0;
            max=0;
            gc = 0;
            while (astarb.GetNumOpenItems() > 0)
            {
                m = astarb.GetOpenItem(0).data;
                astarb.DoSingleSearchStep(p);
                astarb.GetClosedListGCost(m, gc);
                double hCost = h->HCost(t,m);
                if ((gc-hCost) > max){
                    max = gc - hCost;
                    s = m;
                }
            }
        }
    }
    
    // Pivots version
    if (pV == kO){
        ResetEdgeWeights(kEdgeWeight+2);
    }
    
    // Doing search from s
    astarf.SetStopAfterGoal(false);
    astarf.InitializeSearch(ge, s, t, p);
    astarf.SetHeuristic(&z);
    while (astarf.GetNumOpenItems() > 0){
        m = astarf.GetOpenItem(0).data;
        astarf.DoSingleSearchStep(p);
        //g->GetNode(m)->SetLabelF(GraphSearchConstants::kXCoordinate - 1, 1);
    }
    
    // Doing the serch from t
    astarb.SetStopAfterGoal(false);
    astarb.InitializeSearch(ge, t, s, p);
    astarb.SetHeuristic(&z);
    while (astarb.GetNumOpenItems() > 0)
        astarb.DoSingleSearchStep(p);
    
    double dab, dai, dib;
    astarf.GetClosedListGCost(t, dab);
    printf("\n dab %f \n", dab);
    cout<<"s "<<s<<" t "<<t<<endl;
    
    for (int x = 0; x < g->GetNumNodes(); x++)
    {
        node *n = g->GetNode(x);
        if(n->GetLabelF(GraphSearchConstants::kXCoordinate - 1) == 1){
            astarf.GetClosedListGCost(x, dai);
            astarb.GetClosedListGCost(x, dib);
            n->SetLabelF(label, (*f)(dai, dib, dab ));
        }
        else
            n->SetLabelF(label, 0);
    }
    
    
    for (int x = 0; x < g->GetNumEdges(); x++)
    {
        edge *e = g->GetEdge(x);
        double w = fabs(g->GetNode(e->getFrom())->GetLabelF(label) - g->GetNode(e->getTo())->GetLabelF(label));
        //cout<<w<<" ";
        if ((e->GetWeight() - w) > 0)
            e->setWeight(e->GetWeight()-w);
        else if ((e->GetWeight() - w) > -0.0001)
            e->setWeight(0);
        else if ((e->GetWeight() - w) <= -0.0001)
        {
            cout<<"Negative Residual"<<endl;
            exit(0);
        }
    }
    
    printf("\n residual:%f \n", ComputeResidual(g));
    
}


GraphHeuristicContainerE <graphState>* DoMultipleFMDH(int label, int nofmd, heuristicVersion hV, int nofCanPiv, int nofSamples){
    GraphMapHeuristicE<graphState>* h0=new GraphMapHeuristicE<graphState>(map, g);
    GraphHeuristicContainerE <graphState>* h=new GraphHeuristicContainerE <graphState> (g);
    h->AddHeuristic(h0);
    
    
    if (hV == kFaheShe || hV == kFaheSahe){
        for (short i = 0; i < nofmd*2; i = i+2) {
            if(hV == kFaheShe){
                DoOneDimension(label + i, OE, h, kR);
                EmbeddingHeuristic<graphState>* eH = new EmbeddingHeuristic<graphState>(g, label + i, 1);
                GraphHeuristicContainerE <graphState>* hh = new GraphHeuristicContainerE <graphState> (g);
                hh->AddHeuristic(eH);
                DoOneDimension(label + i + 1, DH, hh, kO);
                delete eH;
                delete hh;
                eH = new EmbeddingHeuristic<graphState>(g, label + i, 2);
                h->AddHeuristic(eH);
            }
            
            if(hV == kFaheSahe){
                DoOneDimension(label + i, OE, h, kR);
                EmbeddingHeuristic<graphState>* eH = new EmbeddingHeuristic<graphState>(g, label + i, 1);
                h->AddHeuristic(eH);
                DoOneDimension(label + i + 1, DH, h, kO);
                h->RemoveHeuristic();
                delete eH;
                eH = new EmbeddingHeuristic<graphState>(g, label + i, 2);
                h->AddHeuristic(eH);
            }
            //DoOneDimension(label + i + 1, DH, zH);
            //DoOneDimension(label + i, OE);
            
            //h->AddHeuristic(eH);
           
            //h->RemoveHeuristic();
            //DoOneDimension(label + i + 1, DH);
            
            ResetEdgeWeights(kEdgeWeight+1);
        }
    }
    
    else if (hV == kFaheSf){
        for (short i = 0; i < nofmd*2; i = i+2) {
            DoOneDimension(label + i, OE, h, kR);
            DoOneDimension(label + i + 1, DH);
            EmbeddingHeuristic<graphState>* eH = new EmbeddingHeuristic<graphState>(g, label + i, 2);
            h->AddHeuristic(eH);
            ResetEdgeWeights(kEdgeWeight+1);
        }
    }
        
    else if (hV == kFM9DH){
        DoOneDimension(label, OE, h, kR);
        EmbeddingHeuristic<graphState>* eH = new EmbeddingHeuristic<graphState>(g, label, 1);
        for (int i = 1; i < 9; i++){
            h->AddHeuristic(eH);
            DoOneDimension(label + i, OE, h, kO);
            h->RemoveHeuristic();
            delete eH;
            eH = new EmbeddingHeuristic<graphState>(g, label, i+1);
        }
        h->AddHeuristic(eH);
        DoOneDimension(label + 9, DH, h, kO);
        h->RemoveHeuristic();
        delete eH;
        eH = new EmbeddingHeuristic<graphState>(g, label, 10);
        h->AddHeuristic(eH);
        ResetEdgeWeights(kEdgeWeight+1);
    }
        
    else if (hV == kFM9DH_Fahe){
        DoOneDimension(label, OE, h, kR);
        DoDimensions(label + 1, 9, 1);
        EmbeddingHeuristic<graphState>* eH = new EmbeddingHeuristic<graphState>(g, label, 10);
        h->AddHeuristic(eH);
        ResetEdgeWeights(kEdgeWeight+1);
    }
    
    else if (hV == kFM4DHDH5){
        DoOneDimension(label, OE, h, kR);
        EmbeddingHeuristic<graphState>* eH = new EmbeddingHeuristic<graphState>(g, label, 1);
        for (int i = 1; i < 4; i++){
            h->AddHeuristic(eH);
            DoOneDimension(label + i, OE, h, kO);
            h->RemoveHeuristic();
            delete eH;
            eH = new EmbeddingHeuristic<graphState>(g, label, i+1);
        }
        h->AddHeuristic(eH);
        DoOneDimension(label + 4, DH, h, kO);
        h->RemoveHeuristic();
        delete eH;
        eH = new EmbeddingHeuristic<graphState>(g, label, 5);
        h->AddHeuristic(eH);
        ResetEdgeWeights(kEdgeWeight+1);
        
        DoDH(label + 5, 5);
        DifferentialHeuristic<graphState>* dh = new DifferentialHeuristic<graphState>(g, label + 5, 5);
        h->AddHeuristic(dh);
    }
        
    else if (hV == kFM4DHDH5_Fahe){
        DoOneDimension(label, OE, h, kR);
        DoDimensions(label + 1, 4, 1);
        EmbeddingHeuristic<graphState>* eH = new EmbeddingHeuristic<graphState>(g, label, 5);
        h->AddHeuristic(eH);
        ResetEdgeWeights(kEdgeWeight+1);
        
        DoDH(label + 5, 5);
        DifferentialHeuristic<graphState>* dh = new DifferentialHeuristic<graphState>(g, label + 5, 5);
        h->AddHeuristic(dh);
    }
    
    else if (hV == kDH5FM4DH){
        DoDH(label, 5);
        DifferentialHeuristic<graphState>* dh = new DifferentialHeuristic<graphState>(g, label, 5);
        h->AddHeuristic(dh);
        
        DoOneDimension(label + 5, OE, h, kR);
        EmbeddingHeuristic<graphState>* eH = new EmbeddingHeuristic<graphState>(g, label + 5, 1);
        for (int i = 6; i < 9; i++){
            h->AddHeuristic(eH);
            DoOneDimension(label + i, OE, h, kO);
            h->RemoveHeuristic();
            delete eH;
            eH = new EmbeddingHeuristic<graphState>(g, label + i, i+1);
        }
        h->AddHeuristic(eH);
        DoOneDimension(label + 9, DH, h, kO);
        h->RemoveHeuristic();
        delete eH;
        eH = new EmbeddingHeuristic<graphState>(g, label + 5, 5);
        h->AddHeuristic(eH);
        ResetEdgeWeights(kEdgeWeight+1);
    }
        
    else if (hV == kDH5FM4DH_Fahe){
        DoDH(label, 5);
        DifferentialHeuristic<graphState>* dh = new DifferentialHeuristic<graphState>(g, label, 5);
        h->AddHeuristic(dh);
        
        DoOneDimension(label + 5, OE, h, kR);
        DoDimensions(label + 6, 4, 1);
        EmbeddingHeuristic<graphState>* eH = new EmbeddingHeuristic<graphState>(g, label + 5, 5);
        h->AddHeuristic(eH);
        ResetEdgeWeights(kEdgeWeight+1);
    }
    
    else if (hV == k5FM2){
        for (short i = 0; i < nofmd*2; i = i+2) {
            DoOneDimension(label + i, OE, h, kR);
            EmbeddingHeuristic<graphState>* eH = new EmbeddingHeuristic<graphState>(g, label + i, 1);
            h->AddHeuristic(eH);
            DoOneDimension(label + i + 1, OE, h, kO);
            h->RemoveHeuristic();
            delete eH;
            eH = new EmbeddingHeuristic<graphState>(g, label + i, 2);
            h->AddHeuristic(eH);
            ResetEdgeWeights(kEdgeWeight+1);
        }
    }
    
    else if (hV == k5FM2_Fahe){
        for (short i = 0; i < nofmd*2; i = i+2) {
            DoOneDimension(label + i, OE, h, kR);
            DoOneDimension(label + i + 1, OE);
            EmbeddingHeuristic<graphState>* eH = new EmbeddingHeuristic<graphState>(g, label + i, 2);
            h->AddHeuristic(eH);
            ResetEdgeWeights(kEdgeWeight+1);
        }
    }
    
    else if (hV == k5FM2MED){
        for (short i = 0; i < nofmd*2; i = i+2) {
            DoDimensions(label + i, 2, 0);
            EmbeddingHeuristic<graphState>* eH = new EmbeddingHeuristic<graphState>(g, label + i, 2);
            h->AddHeuristic(eH);
            ResetEdgeWeights(kEdgeWeight+1);
        }
    }
    
    else if (hV == kSubFheShe){
        
        node *n;
        graphState s, m ,t;
        //samples
        srandom(1931);
        std::vector<graphState> samples;
        bool foundNewSample = false;
        for(int i = 0; i < nofSamples; i++){
            foundNewSample = false;
            while (foundNewSample == false){
                n = g->GetRandomNode();
                foundNewSample = true;
                for(int j=0;j<i;j++){
                    if(samples[j]==n->GetNum()){
                        foundNewSample = false;
                        break;
                    }
                }
            }
            samples.push_back(n->GetNum());
        }
        
        
        // finding pivots
        std::vector<graphState> pivots;
        std::vector<graphState> rpivots;
        TemplateAStar<graphState, graphMove, GraphEnvironment> astarf;
        ZeroHeuristic<graphState> z;
        std::vector<graphState> p;
        n = g->GetRandomNode();
        astarf.SetStopAfterGoal(false);
        astarf.InitializeSearch(ge, n->GetNum() , 0, p);
        astarf.SetHeuristic(&z);
        while (astarf.GetNumOpenItems() > 0)
        {
            s = astarf.GetOpenItem(0).data;
            astarf.DoSingleSearchStep(p);
        }
        pivots.push_back(s);
        for(int i = 1; i < 20; i++){
            astarf.SetStopAfterGoal(false);
            astarf.InitializeSearch(ge, pivots[0] , 0, p);
            astarf.SetHeuristic(&z);
            for(int j=1; j<i;j++){
                astarf.AddAdditionalStartState(pivots[j]);
            }
            while (astarf.GetNumOpenItems() > 0)
            {
                s = astarf.GetOpenItem(0).data;
                astarf.DoSingleSearchStep(p);
            }
            pivots.push_back(s);
        }
        for (int i=20; i<nofCanPiv; i++){
            n = g->GetRandomNode();
            s = n->GetNum();
            astarf.SetStopAfterGoal(false);
            astarf.InitializeSearch(ge, s, 0, p);
            astarf.SetHeuristic(&z);
            t=0;
            double max=0;
            double gc=0;
            while (astarf.GetNumOpenItems() > 0){
                m = astarf.GetOpenItem(0).data;
                astarf.DoSingleSearchStep(p);
                astarf.GetClosedListGCost(m, gc);
                double hCost = h->HCost(s,m);
                if ((gc-hCost)>max){
                    max=gc-hCost;
                    t=m;
                }
            }
            pivots.push_back(t);
        }
        
        
        /* Finding pairs of candidates
        for(int j = 0; j < pivots.size(); j++){
            astarf.SetStopAfterGoal(false);
            astarf.InitializeSearch(ge, pivots[j], 0, p);
            astarf.SetHeuristic(&z);
            while (astarf.GetNumOpenItems() > 0)
            {
                astarf.DoSingleSearchStep(p);
            }
            rpivots.push_back(pivots[j]);
            double max=0;
            int maxi=0;
            for(int l =0 ; l < pivots.size(); l++){
                double x=0;
                astarf.GetClosedListGCost(pivots[l], x);
                if(x>max){
                    max=x;
                    maxi=l;
                }
            }
            rpivots.push_back(pivots[maxi]);
        }*/
        
        
        for(int j = 0; j < pivots.size(); j++)
            rpivots.push_back(pivots[j]);
        
        for(int j = 0; j < rpivots.size(); j++){
            cout<<rpivots[j]<<endl;
            DoOneDimension(label + (j * 2), OE, h, kR, rpivots[j]);
            EmbeddingHeuristic<graphState>* eH = new EmbeddingHeuristic<graphState>(g, label + (j * 2), 1);
            GraphHeuristicContainerE <graphState>* hh = new GraphHeuristicContainerE <graphState> (g);
            hh->AddHeuristic(eH);
            DoOneDimension(label + (j * 2) + 1, DH, hh, kO);
            delete eH;
            delete hh;
            ResetEdgeWeights(kEdgeWeight+1);
        }
        
        //for(int j = 0; j < rpivots.size(); j=j+2){
        //    cout<<rpivots[j]<<" "<<rpivots[j+1]<<endl;
        //}
        
        //do the sampling
        double sum=0;
        for(int i = 0; i < nofmd; i++){
            sum = ComputeHeuristic(samples, h);
            cout<<"sum is: "<<sum<<endl;
            int k=0;
            for(int j = 0; j < rpivots.size(); j++){
                EmbeddingHeuristic<graphState> h1(g, label + (j * 2), 2);
                h->AddHeuristic(&h1);
                double newSum = ComputeHeuristic(samples, h);
                
                if (newSum > sum){
                    sum = newSum;
                    k = j * 2;
                }
                h->RemoveHeuristic();
            }
            cout<<sum<<endl;
            cout<<rpivots[k]<<endl;
            EmbeddingHeuristic<graphState>* hh= new EmbeddingHeuristic<graphState> (g, label+k, 2);
            h->AddHeuristic(hh);
        }
        //cout<<h->HCost(15, 1345)<<endl;
    }
    
    else if (hV == kSubFaheShe){
        // Creating candidate pivots after each multiple dimensions
        std::vector<graphState> cPivots;
        TemplateAStar<graphState, graphMove, GraphEnvironment> astarf;
        ZeroHeuristic<graphState> z;
        std::vector<graphState> p;
        graphState s, m, t;
        node *n;
        
        double sum=0;
        srandom(1931);
        std::vector<graphState> samples;
        bool foundNewSample = false;
        for(int i = 0; i < nofSamples; i++){
            foundNewSample = false;
            while (foundNewSample == false){
                n = g->GetRandomNode();
                foundNewSample = true;
                for(int j=0;j<i;j++){
                    if(samples[j]==n->GetNum()){
                        foundNewSample = false;
                        break;
                    }
                }
            }
            samples.push_back(n->GetNum());
        }
        
        for(int i = 0; i < nofmd; i++){
            
            // Finding first candidate pivots
            cPivots.clear();
            n = g->GetRandomNode();
            s = n->GetNum();
            astarf.SetStopAfterGoal(false);
            astarf.InitializeSearch(ge, s, 0, p);
            astarf.SetHeuristic(&z);
            t=0;
            double max=0;
            double gc=0;
            while (astarf.GetNumOpenItems() > 0){
                m = astarf.GetOpenItem(0).data;
                astarf.DoSingleSearchStep(p);
                astarf.GetClosedListGCost(m, gc);
                double hCost = h->HCost(s,m);
                if ((gc-hCost)>max){
                    max=gc-hCost;
                    t=m;
                }
            }
            cPivots.push_back(t);
            DoOneDimension(label + i * 20, DH, h, kO, cPivots[0]);
            EmbeddingHeuristic<graphState>* eHh = new EmbeddingHeuristic<graphState>(g, label + i * 20 , 1);
            h->AddHeuristic(eHh);
            ResetEdgeWeights(kEdgeWeight+1);
            
            // Finding candidate pivots furthust he
            for(int j = 1; j < 10; j++){
                n = g->GetRandomNode();
                s = n->GetNum();
                astarf.SetStopAfterGoal(false);
                astarf.InitializeSearch(ge,  s, 0, p);
                astarf.SetHeuristic(&z);
                t=0;
                double max = 0;
                while (astarf.GetNumOpenItems() > 0){
                    m = astarf.GetOpenItem(0).data;
                    astarf.DoSingleSearchStep(p);
                    double gCost = 0;
                    double hCost = 0;
                    // calculateing the total gcost from all the cpivots and total heurist from them
                    for(int k = 0; k<j; k++){
                        //EmbeddingHeuristic<graphState> h1(g, label + i * 20 + k, 1);
                        //h->AddHeuristic(&h1);
                        hCost += h->HCost(cPivots[k], m);
                        //h->RemoveHeuristic();
                        gCost += g->GetNode(m)->GetLabelF(label + i * 20 + k);
                    }
                    if ((gCost - hCost) > max){
                        max = gCost - hCost;
                        t = m;
                    }
                }
                cPivots.push_back(t);
                DoOneDimension(label + i * 20 + j, DH, h, kO, cPivots[j]);
                EmbeddingHeuristic<graphState>* eH = new EmbeddingHeuristic<graphState>(g, label + i * 20 + j, 1);
                h->AddHeuristic(eH);
                ResetEdgeWeights(kEdgeWeight+1);
            }
            for (int j=0;j<10;j++){
                h->RemoveHeuristic();
            }
            delete eHh;
            
            // Building candidate MD labels
            for(int j = 0; j < cPivots.size(); j++){
                cout<<cPivots[j]<<endl;
                DoOneDimension(label + i * 20 + j * 2, OE, h, kR, cPivots[j]);
                EmbeddingHeuristic<graphState>* eH = new EmbeddingHeuristic<graphState>(g, label + i * 20 + j * 2, 1);
                GraphHeuristicContainerE <graphState>* hh = new GraphHeuristicContainerE <graphState> (g);
                hh->AddHeuristic(eH);
                h->AddHeuristic(eH);
                DoOneDimension(label + i * 20 + j * 2 + 1, DH, hh, kO);
                h->RemoveHeuristic();
                delete eH;
                delete hh;
                ResetEdgeWeights(kEdgeWeight+1);
            }
            
            // Sampling
            sum = ComputeHeuristic(samples, h);
            cout<<"sum is: "<<sum<<endl;
            int k=0;
            for(int j = 0; j < cPivots.size(); j++){
                EmbeddingHeuristic<graphState> h1(g, label + i * 20 + j * 2, 2);
                h->AddHeuristic(&h1);
                double newSum = ComputeHeuristic(samples, h);
                
                if (newSum > sum){
                    sum = newSum;
                    k = j;
                }
                h->RemoveHeuristic();
            }
            cout<<sum<<endl;
            cout<<cPivots[k]<<endl;
            EmbeddingHeuristic<graphState>* hh= new EmbeddingHeuristic<graphState> (g, label + i * 20 + k * 2, 2);
            h->AddHeuristic(hh);
        }
    }
    
    else if (hV == kSubFheSheThe){
        
        node *n;
        graphState s, m ,t;
        //samples
        std::vector<graphState> samples;
        srandom(1931);
        bool foundNewSample = false;
        for(int i = 0; i < nofSamples; i++){
            foundNewSample = false;
            while (foundNewSample == false){
                n = g->GetRandomNode();
                foundNewSample = true;
                for(int j=0;j<i;j++){
                    if(samples[j]==n->GetNum()){
                        foundNewSample = false;
                        break;
                    }
                }
            }
            samples.push_back(n->GetNum());
        }
        
        
        // finding pivots
        std::vector<graphState> pivots;
        std::vector<graphState> rpivots;
        TemplateAStar<graphState, graphMove, GraphEnvironment> astarf;
        ZeroHeuristic<graphState> z;
        std::vector<graphState> p;
        n = g->GetRandomNode();
        astarf.SetStopAfterGoal(false);
        astarf.InitializeSearch(ge, n->GetNum() , 0, p);
        astarf.SetHeuristic(&z);
        while (astarf.GetNumOpenItems() > 0)
        {
            s = astarf.GetOpenItem(0).data;
            astarf.DoSingleSearchStep(p);
        }
        pivots.push_back(s);
        for(int i = 1; i < 15; i++){
            astarf.SetStopAfterGoal(false);
            astarf.InitializeSearch(ge, pivots[0] , 0, p);
            astarf.SetHeuristic(&z);
            for(int j=1; j<i;j++){
                astarf.AddAdditionalStartState(pivots[j]);
            }
            while (astarf.GetNumOpenItems() > 0)
            {
                s = astarf.GetOpenItem(0).data;
                astarf.DoSingleSearchStep(p);
            }
            pivots.push_back(s);
        }
        for (int i=15; i<25; i++){
            n = g->GetRandomNode();
            s = n->GetNum();
            astarf.SetStopAfterGoal(false);
            astarf.InitializeSearch(ge, s, 0, p);
            astarf.SetHeuristic(&z);
            t=0;
            double max=0;
            double gc=0;
            while (astarf.GetNumOpenItems() > 0){
                m = astarf.GetOpenItem(0).data;
                astarf.DoSingleSearchStep(p);
                astarf.GetClosedListGCost(m, gc);
                double hCost = h->HCost(s,m);
                if ((gc-hCost)>max){
                    max=gc-hCost;
                    t=m;
                }
            }
            pivots.push_back(t);
        }
        
        for(int j = 0; j < pivots.size(); j++)
            rpivots.push_back(pivots[j]);
        
        for(int j = 0; j < rpivots.size(); j++){
            cout<<rpivots[j]<<endl;
            DoOneDimension(label + (j * 3), OE, h, kR, rpivots[j]);
            EmbeddingHeuristic<graphState>* eH = new EmbeddingHeuristic<graphState>(g, label + (j * 3), 1);
            GraphHeuristicContainerE <graphState>* hh = new GraphHeuristicContainerE <graphState> (g);
            hh->AddHeuristic(eH);
            DoOneDimension(label + (j * 3) + 1, OE, hh, kO);
            hh->RemoveHeuristic();
            eH = new EmbeddingHeuristic<graphState>(g, label + (j * 3), 2);
            hh->AddHeuristic(eH);
            DoOneDimension(label + (j * 3) + 2, DH, hh, kO);
            delete eH;
            delete hh;
            /*
            DoOneDimension(label + (j * 3), OE);
            DoOneDimension(label + (j * 3) + 1, OE);
            DoOneDimension(label + (j * 3) + 2, DH);
            */
            ResetEdgeWeights(kEdgeWeight+1);
        }
        for(int j = 0; j < rpivots.size(); j++){
            cout<<rpivots[j]<<endl;
            DoOneDimension(label + rpivots.size() * 3 + j, DH);
            ResetEdgeWeights(kEdgeWeight+1);
        }
        
        //for(int j = 0; j < rpivots.size(); j=j+2){
        //    cout<<rpivots[j]<<" "<<rpivots[j+1]<<endl;
        //}
        
        //do the sampling
        double sum=0;
        for(int i = 0; i < nofmd; i++){
            sum = ComputeHeuristic(samples, h);
            cout<<"sum is: "<<sum<<endl;
            int k=0;
            for(int j = 0; j < rpivots.size(); j++){
                EmbeddingHeuristic<graphState> h1(g, label + (j * 3), 3);
                h->AddHeuristic(&h1);
                double newSum = ComputeHeuristic(samples, h);
                
                if (newSum > sum){
                    sum = newSum;
                    k = j * 3;
                }
                h->RemoveHeuristic();
            }
            cout<<sum<<endl;
            cout<<rpivots[k]<<endl;
            EmbeddingHeuristic<graphState>* hh= new EmbeddingHeuristic<graphState> (g, label+k, 3);
            h->AddHeuristic(hh);
        }
        for(int i = 0; i < 1; i++){
            sum = ComputeHeuristic(samples, h);
            cout<<"sum is: "<<sum<<endl;
            int k=0;
            for(int j = 0; j < rpivots.size(); j++){
                EmbeddingHeuristic<graphState> h1(g, label + rpivots.size() * 3 + j, 1);
                h->AddHeuristic(&h1);
                double newSum = ComputeHeuristic(samples, h);
                
                if (newSum > sum){
                    sum = newSum;
                    k = rpivots.size() * 3 + j;
                }
                h->RemoveHeuristic();
            }
            cout<<sum<<endl;
            cout<<rpivots[k]<<endl;
            EmbeddingHeuristic<graphState>* hh= new EmbeddingHeuristic<graphState> (g, label+k, 1);
            h->AddHeuristic(hh);
        }
        //cout<<h->HCost(15, 1345)<<endl;
    }
    
    else if (hV == kSubFheSheTheFhe){
        
        node *n;
        graphState s, m ,t;
        //samples
        std::vector<graphState> samples;
        for(int i = 0; i < nofSamples; i++){
            n = g->GetRandomNode();
            samples.push_back(n->GetNum());
        }
        
        
        // finding pivots
        std::vector<graphState> pivots;
        std::vector<graphState> rpivots;
        TemplateAStar<graphState, graphMove, GraphEnvironment> astarf;
        ZeroHeuristic<graphState> z;
        std::vector<graphState> p;
        n = g->GetRandomNode();
        astarf.SetStopAfterGoal(false);
        astarf.InitializeSearch(ge, n->GetNum() , 0, p);
        astarf.SetHeuristic(&z);
        while (astarf.GetNumOpenItems() > 0)
        {
            s = astarf.GetOpenItem(0).data;
            astarf.DoSingleSearchStep(p);
        }
        pivots.push_back(s);
        for(int i = 1; i < 15; i++){
            astarf.SetStopAfterGoal(false);
            astarf.InitializeSearch(ge, pivots[0] , 0, p);
            astarf.SetHeuristic(&z);
            for(int j=1; j<i;j++){
                astarf.AddAdditionalStartState(pivots[j]);
            }
            while (astarf.GetNumOpenItems() > 0)
            {
                s = astarf.GetOpenItem(0).data;
                astarf.DoSingleSearchStep(p);
            }
            pivots.push_back(s);
        }
        
        for (int i=15; i<20; i++){
            n = g->GetRandomNode();
            s = n->GetNum();
            astarf.SetStopAfterGoal(false);
            astarf.InitializeSearch(ge, s, 0, p);
            astarf.SetHeuristic(&z);
            t=0;
            double max=0;
            double gc=0;
            while (astarf.GetNumOpenItems() > 0){
                m = astarf.GetOpenItem(0).data;
                astarf.DoSingleSearchStep(p);
                astarf.GetClosedListGCost(m, gc);
                double hCost = h->HCost(s,m);
                if ((gc-hCost)>max){
                    max=gc-hCost;
                    t=m;
                }
            }
            pivots.push_back(t);
        }
         
        /*
        for (int i=15; i<20; i++){
            bool corner = false;
            do{
                n = g->GetRandomNode();
                s = n->GetNum();
                if(n->getNeighborIter())
            }while(corner == false);
             
            n = g->GetRandomNode();
            s = n->GetNum();
            pivots.push_back(s);
        }
        */
    
        for(int j = 0; j < pivots.size(); j++)
            rpivots.push_back(pivots[j]);
        
        for(int j = 0; j < rpivots.size(); j++){
            cout<<rpivots[j]<<endl;
            DoOneDimension(label + (j * 4), OE, h, kR, rpivots[j]);
            EmbeddingHeuristic<graphState>* eH = new EmbeddingHeuristic<graphState>(g, label + (j * 4), 1);
            GraphHeuristicContainerE <graphState>* hh = new GraphHeuristicContainerE <graphState> (g);
            hh->AddHeuristic(eH);
            DoOneDimension(label + (j * 4) + 1, OE, hh, kO);
            hh->RemoveHeuristic();
            eH = new EmbeddingHeuristic<graphState>(g, label + (j * 4), 2);
            hh->AddHeuristic(eH);
            DoOneDimension(label + (j * 4) + 2, OE, hh, kO);
            hh->RemoveHeuristic();
            eH = new EmbeddingHeuristic<graphState>(g, label + (j * 4), 3);
            hh->AddHeuristic(eH);
            DoOneDimension(label + (j * 4) + 3, DH, hh, kO);
            delete eH;
            delete hh;
            /*
            DoOneDimension(label + (j * 3), OE);
            DoOneDimension(label + (j * 3) + 1, OE);
            DoOneDimension(label + (j * 3) + 2, DH);
            */
            ResetEdgeWeights(kEdgeWeight+1);
        }
        for(int j = 0; j < rpivots.size(); j++){
            cout<<rpivots[j]<<endl;
            DoOneDimension(label + rpivots.size() * 4 + j, DH);
            ResetEdgeWeights(kEdgeWeight+1);
        }
        
        //for(int j = 0; j < rpivots.size(); j=j+2){
        //    cout<<rpivots[j]<<" "<<rpivots[j+1]<<endl;
        //}
        
        //do the sampling
        double sum=0;
        for(int i = 0; i < nofmd; i++){
            sum = ComputeHeuristic(samples, h);
            
            cout<<"sum is: "<<sum<<endl;
            int k=0;
            for(int j = 0; j < rpivots.size(); j++){
                EmbeddingHeuristic<graphState> h1(g, label + (j * 4), 4);
                h->AddHeuristic(&h1);
                double newSum = ComputeHeuristic(samples, h);
                
                if (newSum > sum){
                    sum = newSum;
                    k = j * 4;
                }
                h->RemoveHeuristic();
            }
            cout<<sum<<endl;
            cout<<rpivots[k]<<endl;
            EmbeddingHeuristic<graphState>* hh= new EmbeddingHeuristic<graphState> (g, label+k, 4);
            h->AddHeuristic(hh);
        }
        for(int i = 0; i < 2; i++){
            sum = ComputeHeuristic(samples, h);
            cout<<"sum is: "<<sum<<endl;
            int k=0;
            for(int j = 0; j < rpivots.size(); j++){
                EmbeddingHeuristic<graphState> h1(g, label + rpivots.size() * 4 + j, 1);
                h->AddHeuristic(&h1);
                double newSum = ComputeHeuristic(samples, h);
                
                if (newSum > sum){
                    sum = newSum;
                    k = rpivots.size() * 4 + j;
                }
                h->RemoveHeuristic();
            }
            cout<<sum<<endl;
            cout<<rpivots[k]<<endl;
            EmbeddingHeuristic<graphState>* hh= new EmbeddingHeuristic<graphState> (g, label+k, 1);
            h->AddHeuristic(hh);
        }
        //cout<<h->HCost(15, 1345)<<endl;
    }
    
    else if (hV == kSub5){
        
        node *n;
        graphState s, m ,t;
        //samples
        std::vector<graphState> samples;
        for(int i = 0; i < nofSamples; i++){
            n = g->GetRandomNode();
            samples.push_back(n->GetNum());
        }
        
        
        // finding pivots
        std::vector<graphState> pivots;
        std::vector<graphState> rpivots;
        TemplateAStar<graphState, graphMove, GraphEnvironment> astarf;
        ZeroHeuristic<graphState> z;
        std::vector<graphState> p;
        n = g->GetRandomNode();
        astarf.SetStopAfterGoal(false);
        astarf.InitializeSearch(ge, n->GetNum() , 0, p);
        astarf.SetHeuristic(&z);
        while (astarf.GetNumOpenItems() > 0)
        {
            s = astarf.GetOpenItem(0).data;
            astarf.DoSingleSearchStep(p);
        }
        pivots.push_back(s);
        for(int i = 1; i < 15; i++){
            astarf.SetStopAfterGoal(false);
            astarf.InitializeSearch(ge, pivots[0] , 0, p);
            astarf.SetHeuristic(&z);
            for(int j=1; j<i;j++){
                astarf.AddAdditionalStartState(pivots[j]);
            }
            while (astarf.GetNumOpenItems() > 0)
            {
                s = astarf.GetOpenItem(0).data;
                astarf.DoSingleSearchStep(p);
            }
            pivots.push_back(s);
        }
        
        for (int i=15; i<20; i++){
            n = g->GetRandomNode();
            s = n->GetNum();
            astarf.SetStopAfterGoal(false);
            astarf.InitializeSearch(ge, s, 0, p);
            astarf.SetHeuristic(&z);
            t=0;
            double max=0;
            double gc=0;
            while (astarf.GetNumOpenItems() > 0){
                m = astarf.GetOpenItem(0).data;
                astarf.DoSingleSearchStep(p);
                astarf.GetClosedListGCost(m, gc);
                double hCost = h->HCost(s,m);
                if ((gc-hCost)>max){
                    max=gc-hCost;
                    t=m;
                }
            }
            pivots.push_back(t);
        }
        
        for(int j = 0; j < pivots.size(); j++)
            rpivots.push_back(pivots[j]);
        
        for(int j = 0; j < rpivots.size(); j++){
            cout<<rpivots[j]<<endl;
            DoOneDimension(label + (j * 5), OE, h, kR, rpivots[j]);
            EmbeddingHeuristic<graphState>* eH = new EmbeddingHeuristic<graphState>(g, label + (j * 5), 1);
            GraphHeuristicContainerE <graphState>* hh = new GraphHeuristicContainerE <graphState> (g);
            hh->AddHeuristic(eH);
            DoOneDimension(label + (j * 5) + 1, OE, hh, kO);
            hh->RemoveHeuristic();
            eH = new EmbeddingHeuristic<graphState>(g, label + (j * 5), 2);
            hh->AddHeuristic(eH);
            DoOneDimension(label + (j * 5) + 2, OE, hh, kO);
            hh->RemoveHeuristic();
            eH = new EmbeddingHeuristic<graphState>(g, label + (j * 5), 3);
            hh->AddHeuristic(eH);
            DoOneDimension(label + (j * 5) + 3, OE, hh, kO);
            hh->RemoveHeuristic();
            eH = new EmbeddingHeuristic<graphState>(g, label + (j * 5), 4);
            hh->AddHeuristic(eH);
            DoOneDimension(label + (j * 5) + 4, DH, hh, kO);
            delete eH;
            delete hh;
            /*
            DoOneDimension(label + (j * 3), OE);
            DoOneDimension(label + (j * 3) + 1, OE);
            DoOneDimension(label + (j * 3) + 2, DH);
            */
            ResetEdgeWeights(kEdgeWeight+1);
        }
        
        //for(int j = 0; j < rpivots.size(); j=j+2){
        //    cout<<rpivots[j]<<" "<<rpivots[j+1]<<endl;
        //}
        
        //do the sampling
        double sum=0;
        for(int i = 0; i < nofmd; i++){
            sum = ComputeHeuristic(samples, h);
            cout<<"sum is: "<<sum<<endl;
            int k=0;
            for(int j = 0; j < rpivots.size(); j++){
                EmbeddingHeuristic<graphState> h1(g, label + (j * 5), 5);
                h->AddHeuristic(&h1);
                double newSum = ComputeHeuristic(samples, h);
                
                if (newSum > sum){
                    sum = newSum;
                    k = j * 5;
                }
                h->RemoveHeuristic();
            }
            cout<<sum<<endl;
            cout<<rpivots[k]<<endl;
            EmbeddingHeuristic<graphState>* hh= new EmbeddingHeuristic<graphState> (g, label+k, 5);
            h->AddHeuristic(hh);
        }
    }
    
    return h;
}

GraphHeuristicContainerE <graphState>* DoMultipleFMDHI(int label, int nofmd, short nofcp, short nofSamples){
    std::vector<graphState> samples;
    node *n;
    
    
    
    //find pivots
    std::vector<graphState> pivots;
    std::vector<graphState> rpivots;
    TemplateAStar<graphState, graphMove, GraphEnvironment> astarf;
    ZeroHeuristic<graphState> z;
    std::vector<graphState> p;
    graphState s;
    long wi=map->GetMapWidth();
    long hi=map->GetMapHeight();
    int counter=0;
    for(int i = 0; i <nofcp; i++){
        for(int j = 0; j <nofcp; j++){
            if(map->GetNodeNum((wi/(nofcp-1))*j,(hi/(nofcp-1))*i)==-1){
                bool flag=false;
                cout<<"ahhhhhhhhhhhhhhhhhhh"<<endl;
                cout<<(wi/(nofcp-1))*j<<" "<<(hi/(nofcp-1))*i<<endl;
                for (int m=3; m<wi; m+=2){
                    if (flag==true)
                        break;
                    int r=(hi/(nofcp-1))*i-m/2;
                    int c=(wi/(nofcp-1))*j-m/2;
                    for (int k=0; k<m; k++){
                        if(flag==true)
                            break;
                        for (int l=0; l<m; l++){
                            if(map->GetNodeNum(c+l, r+k)!=-1){
                                pivots.push_back(map->GetNodeNum(c+l,r+k ));
                                //xs[counter]=c+l;
                                //ys[counter]=r+k;
                                cout<<counter<<": "<<c+l<<" "<<r+k<<endl;
                                counter++;
                                flag=true;
                                break;
                            }
                        }
                    }
                }
            }
            else{
                pivots.push_back(map->GetNodeNum((wi/(nofcp-1))*j, (hi/(nofcp-1))*i));
                cout<<counter<<" asli: " <<(wi/(nofcp-1))*j<<" "<<(hi/(nofcp-1))*i<<endl;
                //xs[counter]=(wi/(nofcp-1))*j;
                //ys[counter]=(hi/(nofcp-1))*i;
                counter++;
                
            }
        }
        
    }
    
    

    for(int j = 0; j < pivots.size(); j++){
        cout<<pivots[j]<<endl;
        DoTwoDimensions(label+(j*2),OE, pivots[j]);
        ResetEdgeWeights(kEdgeWeight+1);
    }
    
    //samples
    /*nofcp=28;
    for(int i = 0; i <nofcp; i++){
        for(int j = 0; j <nofcp; j++){
            if(map->GetNodeNum((wi/(nofcp-1))*j,(hi/(nofcp-1))*i)==-1){
                bool flag=false;
                for (int m=3; m<wi; m+=2){
                    if (flag==true)
                        break;
                    int r=(hi/(nofcp-1))*i-m/2;
                    int c=(wi/(nofcp-1))*j-m/2;
                    for (int k=0; k<m; k++){
                        if(flag==true)
                            break;
                        for (int l=0; l<m; l++){
                            if(map->GetNodeNum(c+l, r+k)!=-1){
                                samples.push_back(map->GetNodeNum(c+l,r+k ));
                                flag=true;
                                break;
                            }
                        }
                    }
                }
            }
            else{
                samples.push_back(map->GetNodeNum((wi/(nofcp-1))*j, (hi/(nofcp-1))*i));
            }
        }
        
    }*/
    
    for(int i = 0; i < nofSamples; i++){
        n = g->GetRandomNode();
        samples.push_back(n->GetNum());
    }
    
    //do the sampling
    GraphMapHeuristicE<graphState>* h0=new GraphMapHeuristicE<graphState>(map, g);
    GraphHeuristicContainerE <graphState>* h=new GraphHeuristicContainerE <graphState> (g);
    h->AddHeuristic(h0);
    double sum=0;
    for(int i = 0; i < nofmd; i++){
        sum = ComputeHeuristic(samples, h);
        cout<<"sum is: "<<sum<<endl;
        int k=0;
        for(int j = 0; j < pivots.size(); j++){
            EmbeddingHeuristic<graphState> h1(g, label+(j*2), 2);
            h->AddHeuristic(&h1);
            double s= ComputeHeuristic(samples, h);
            
            if (s>sum){
                sum=s;
                k=j;
            }
            h->RemoveHeuristic();
        }
        cout<<sum<<endl;
        cout<<pivots[k]<<endl;
        iindex[i]=k;
        EmbeddingHeuristic<graphState>* hh= new EmbeddingHeuristic<graphState> (g, label+(k*2), 2);
        h->AddHeuristic(hh);
    }
    //cout<<h->HCost(15, 1345)<<endl;
    return h;

}

void DoOneLineEmbedding(int label)
{
    //ResetSeenLabels();
    printf("\n Dimension %d \n\n", label);
    TemplateAStar<graphState, graphMove, GraphEnvironment> astarf;
    TemplateAStar<graphState, graphMove, GraphEnvironment> astarb;
    std::vector<graphState> p;
    node *n = g->GetRandomNode();
    graphState s, m, t;
    ZeroHeuristic<graphState> z;
   
    srandom(3);
    // Find a node in largest part
    while (n->GetLabelF(GraphSearchConstants::kXCoordinate - 1) != 1){
        n = g->GetRandomNode();
    }
    
    // Doing a temporary first dimension to find first pivot of the line and prepare it for the second temporary dimension
    astarf.SetStopAfterGoal(false);
    astarf.InitializeSearch(ge, n->GetNum(), 0, p);
    astarf.SetHeuristic(&z);
    while (astarf.GetNumOpenItems() > 0)
    {
        t = astarf.GetOpenItem(0).data;
        astarf.DoSingleSearchStep(p);
    }
    
    astarb.SetStopAfterGoal(false);
    astarb.InitializeSearch(ge, t, 0, p);
    astarb.SetHeuristic(&z);
    while (astarb.GetNumOpenItems() > 0)
    {
        s = astarb.GetOpenItem(0).data;
        astarb.DoSingleSearchStep(p);
    }
    
    astarf.SetStopAfterGoal(false);
    astarf.InitializeSearch(ge, s, t, p);
    astarf.SetHeuristic(&z);
    while (astarf.GetNumOpenItems() > 0)
        astarf.DoSingleSearchStep(p);

    double dab, dai, dib;
    astarf.GetClosedListGCost(t, dab);
    cout<<" s1: "<<s<<" t1: "<<t<<endl;
    printf(" dab %f \n", dab);
    for (int x = 0; x < g->GetNumNodes(); x++)
    {
        node *n = g->GetNode(x);
        if(n->GetLabelF(GraphSearchConstants::kXCoordinate - 1) == 1){
            astarf.GetClosedListGCost(x, dai);
            astarb.GetClosedListGCost(x, dib);
            n->SetLabelF(label, (dai + dab - dib)/2);
        }
        else
            n->SetLabelF(label, 0);
    }
    for (int x = 0; x < g->GetNumEdges(); x++)
    {
        edge *e = g->GetEdge(x);
        double w = fabs(g->GetNode(e->getFrom())->GetLabelF(label) - g->GetNode(e->getTo())->GetLabelF(label));
        //cout<<w<<" ";
        if ((e->GetWeight() - w) > 0)
            e->setWeight(e->GetWeight()-w);
        else if ((e->GetWeight() - w) > -0.0001)
            e->setWeight(0);
        else if ((e->GetWeight() - w) <= -0.0001)
        {
            cout<<"Negative Residual"<<endl;
            exit(0);
        }
    }

    //Second Teporary Dimension
    n = g->GetRandomNode();
    graphState s2, t2;
    
    // Find a node in largest part
    while (n->GetLabelF(GraphSearchConstants::kXCoordinate - 1) != 1){
        n = g->GetRandomNode();
    }
    
    astarf.SetStopAfterGoal(false);
    astarf.InitializeSearch(ge, n->GetNum(), 0, p);
    astarf.SetHeuristic(&z);
    while (astarf.GetNumOpenItems() > 0)
    {
        t2 = astarf.GetOpenItem(0).data;
        astarf.DoSingleSearchStep(p);
    }
    
    astarb.SetStopAfterGoal(false);
    astarb.InitializeSearch(ge, t2, 0, p);
    astarb.SetHeuristic(&z);
    while (astarb.GetNumOpenItems() > 0)
    {
        s2 = astarb.GetOpenItem(0).data;
        astarb.DoSingleSearchStep(p);
    }
    cout<<" s2: "<<s2<<" t2: "<<t2<<endl;
    
    ResetEdgeWeights(kEdgeWeight+1);
    
    // Finding the line
    astarf.SetStopAfterGoal(true);
    astarf.InitializeSearch(ge, s, s2, p);
    astarf.SetHeuristic(&z);
    astarf.GetPath(ge, s, s2, p);
    vector<graphState> line;
    for(int i=0; i<p.size(); i++){
        line.push_back(p[i]);
        //xs.push_back(ge->GetLocation(p[i]).x);
        //ys.push_back(ge->GetLocation(p[i]).y);
    }
    
    // Doing the embedding
    astarf.SetStopAfterGoal(false);
    astarf.InitializeSearch(ge, s, s2, p);
    astarf.SetHeuristic(&z);
    for(int k = 1; k < line.size(); k++){
        astarf.AddAdditionalStartState(line[k]);
    }
    while (astarf.GetNumOpenItems() > 0)
        astarf.DoSingleSearchStep(p);
    
    for (int x = 0; x < g->GetNumNodes(); x++)
    {
        node *n = g->GetNode(x);
        if(n->GetLabelF(GraphSearchConstants::kXCoordinate - 1) == 1){
            astarf.GetClosedListGCost(x, dai);
            n->SetLabelF(label, dai);
        }
        else
            n->SetLabelF(label, 0);
    }
    for (int x = 0; x < g->GetNumEdges(); x++)
    {
        edge *e = g->GetEdge(x);
        double w = fabs(g->GetNode(e->getFrom())->GetLabelF(label) - g->GetNode(e->getTo())->GetLabelF(label));
        //cout<<w<<" ";
        if ((e->GetWeight() - w) > 0)
            e->setWeight(e->GetWeight()-w);
        else if ((e->GetWeight() - w) > -0.0001)
            e->setWeight(0);
        else if ((e->GetWeight() - w) <= -0.0001)
        {
            cout<<"Negative Residual"<<endl;
            exit(0);
        }
    }
    printf("\n residual:%f \n", ComputeResidual(g));
    
    astarf.SetStopAfterGoal(true);
    astarf.InitializeSearch(ge, s, s2, p);
    astarf.SetHeuristic(&z);
    astarf.GetPath(ge, s, s2, p);
    for(int i=0; i<p.size()-1; i++){
        xs.push_back(ge->GetLocation(p[i]).x);
        ys.push_back(ge->GetLocation(p[i]).y);
        cout<<g->FindEdge(p[i], p[i+1])->GetWeight()<<" ";
    }
    cout<<"path length between first pair: "<<ge->GetPathLength(p)<<endl;
}


GraphHeuristicContainerE <graphState>* DoLineH(int label, int nofmd, heuristicVersion hV, int nofCanPiv, int nofSamples){
    GraphMapHeuristicE<graphState>* h0=new GraphMapHeuristicE<graphState>(map, g);
    GraphHeuristicContainerE <graphState>* h=new GraphHeuristicContainerE <graphState> (g);
    h->AddHeuristic(h0);
    DoOneLineEmbedding(label);
    DoOneDimension(label+1, OE);
    EmbeddingHeuristic<graphState>* h01 = new EmbeddingHeuristic<graphState>(g, label, 2);
    h->AddHeuristic(h01);
    ResetEdgeWeights(kEdgeWeight+1);
    return h;
}

void DoDH(int startlabel, int nofp)
{
    //ResetSeenLabels();
    
    std::vector<graphState> pivots;
    TemplateAStar<graphState, graphMove, GraphEnvironment> astarf;
    std::vector<graphState> p;
    short int label=startlabel;
    ZeroHeuristic<graphState> z;
    srandom(78671);
    node *n = g->GetRandomNode();
    cout<<n->GetNum()<<endl;
    graphState s, m, t;
    
    
    // Find a node in largest part
    while (n->GetLabelF(GraphSearchConstants::kXCoordinate - 1) != 1){
        n = g->GetRandomNode();
    }
    
    // Find the first Pivot
    astarf.SetStopAfterGoal(false);
    astarf.InitializeSearch(ge, n->GetNum() , 0, p);
    astarf.SetHeuristic(&z);
    while (astarf.GetNumOpenItems() > 0)
    {
        t = astarf.GetOpenItem(0).data;
        astarf.DoSingleSearchStep(p);
    }
    pivots.push_back(t);
    
    // Finding other pivots in largest part
    for(int j = 1; j < nofp; j++){
        astarf.SetStopAfterGoal(false);
        astarf.InitializeSearch(ge, pivots[0] , 0, p);
        astarf.SetHeuristic(&z);
        for(int k = 1; k < j; k++){
            astarf.AddAdditionalStartState(pivots[k]);
        }
        while (astarf.GetNumOpenItems() > 0)
        {
            s = astarf.GetOpenItem(0).data;
            astarf.DoSingleSearchStep(p);
        }
        pivots.push_back(s);
    }

    for(int j = 0; j < nofp; j++)
            cout<<pivots[j]<<" ";
    //xs.push_back(ge->GetLocation(pivots[i]).x);
    //ys.push_back(ge->GetLocation(pivots[i]).y);
    cout<<endl;
    
    // Labeling
    for(int j = 0; j < nofp; j++){
        astarf.SetStopAfterGoal(false);
        astarf.InitializeSearch(ge, pivots[j], 0, p);
        astarf.SetHeuristic(&z);
        while (astarf.GetNumOpenItems() > 0)
            astarf.DoSingleSearchStep(p);
            
        for (int x = 0; x < g->GetNumNodes(); x++)
        {
            n = g->GetNode(x);
            if(n->GetLabelF(GraphSearchConstants::kXCoordinate - 1) == 1){
                double dai;
                astarf.GetClosedListGCost(x, dai);
                n->SetLabelF(label + j, dai);
            }
            else
                n->SetLabelF(label + j, 0);
        }
    }
    
}

void DoGDH(int startlabel, int nofp, short nofcp, short nofSamples){

    std::vector<graphState> pivots;
    TemplateAStar<graphState, graphMove, GraphEnvironment> astarf;
    std::vector<graphState> p;
    short int label=startlabel;
    ZeroHeuristic<graphState> z;
    
    GraphMapHeuristicE<graphState> h0(map, g);
    GraphHeuristicContainerE <graphState> h(g);
    h.AddHeuristic(&h0);
    
    node *n;
    
    graphState s;
    int k=4;
    
    // Samples
    std::vector<graphState> samples;
    cout<<"\nsamples"<<endl;
    srandom(1931);
    for(int i = 0; i < nofSamples; i++){
        bool foundNewSample = false;
        while (foundNewSample == false){
            n = g->GetRandomNode();
            foundNewSample = true;
            for(int j=0;j<i;j++){
                if(samples[j]==n->GetNum()){
                    foundNewSample = false;
                    break;
                }
            }
        }
        samples.push_back(n->GetNum());
    }
    
    astarf.SetStopAfterGoal(false);
    astarf.InitializeSearch(ge, n->GetNum() , 0, p);
    astarf.SetHeuristic(&z);
    
    while (astarf.GetNumOpenItems() > 0)
    {
        s = astarf.GetOpenItem(0).data;
        astarf.DoSingleSearchStep(p);
    }
    pivots.push_back(s);
    
    //finding pivots
    for(int i = 1; i < 20; i++){
        astarf.SetStopAfterGoal(false);
        astarf.InitializeSearch(ge, pivots[0] , 0, p);
        astarf.SetHeuristic(&z);
        for(int j=1; j<i;j++){
            astarf.AddAdditionalStartState(pivots[j]);
        }
        while (astarf.GetNumOpenItems() > 0)
        {
            s = astarf.GetOpenItem(0).data;
            astarf.DoSingleSearchStep(p);
        }
        pivots.push_back(s);
    }
    
    /*
    for (int i=20; i<nofcp; i++){
        bool flag=false;
        while (flag==false){
            n = g->GetRandomNode();
            flag=true;
            for(int j=0;j<i;j++){
                if(pivots[j]==n->GetNum() || g->GetNode(n->GetNum())->getNumOutgoingEdges()>3){
                    flag=false;
                    break;
                }
            }
        }
        for(int j=0;j<i;j++){
            if(pivots[j]==n->GetNum()){
                cout<<"duplicate CPivot"<<endl;
                exit(0);
            }
        }
        pivots.push_back(n->GetNum());
        
        //pivots.push_back(random()%g->GetNumNodes());
    }
     */
    graphState t,m;
    for (int i=20; i<nofcp; i++){
        n = g->GetRandomNode();
        s = n->GetNum();
        astarf.SetStopAfterGoal(false);
        astarf.InitializeSearch(ge, s, 0, p);
        astarf.SetHeuristic(&z);
        t=0;
        double max=0;
        double gc=0;
        while (astarf.GetNumOpenItems() > 0){
            m = astarf.GetOpenItem(0).data;
            astarf.DoSingleSearchStep(p);
            astarf.GetClosedListGCost(m, gc);
            double hCost = h.HCost(s,m);
            if ((gc-hCost)>max){
                max=gc-hCost;
                t=m;
            }
        }
        pivots.push_back(t);
    }
    
    /*long wi=map->GetMapWidth();
    long hi=map->GetMapHeight();
    
    int counter=0;
    for(int i = 0; i <nofcp; i++){
        for(int j = 0; j <nofcp; j++){
            if(map->GetNodeNum((wi/(nofcp-1))*j,(hi/(nofcp-1))*i)==-1){
                bool flag=false;
                cout<<"ahhhhhhhhhhhhhhhhhhh"<<endl;
                cout<<(wi/(nofcp-1))*j<<" "<<(hi/(nofcp-1))*i<<endl;
                for (int m=3; m<wi; m+=2){
                    if (flag==true)
                        break;
                    int r=(hi/(nofcp-1))*i-m/2;
                    int c=(wi/(nofcp-1))*j-m/2;
                    for (int k=0; k<m; k++){
                        if(flag==true)
                            break;
                        for (int l=0; l<m; l++){
                            if(map->GetNodeNum(c+l, r+k)!=-1){
                                pivots.push_back(map->GetNodeNum(c+l,r+k ));
                                //xs[counter]=c+l;
                                //ys[counter]=r+k;
                                cout<<counter<<": "<<c+l<<" "<<r+k<<endl;
                                counter++;
                                flag=true;
                                break;
                            }
                        }
                    }
                }
            }
            else{
                pivots.push_back(map->GetNodeNum((wi/(nofcp-1))*j, (hi/(nofcp-1))*i));
                cout<<counter<<" asli: " <<(wi/(nofcp-1))*j<<" "<<(hi/(nofcp-1))*i<<endl;
                //xs[counter]=(wi/(nofcp-1))*j;
                //ys[counter]=(hi/(nofcp-1))*i;
                counter++;
                
            }
        }
        
    }*/
        

    
    for(int i = 0; i < pivots.size(); i++)
        cout<<pivots[i]<<" ";
    cout<<endl;
    
    
    //Doing Embedding
    for(int i = 0; i < pivots.size(); i++){
        astarf.SetStopAfterGoal(false);
        astarf.InitializeSearch(ge, pivots[i], 0, p);
        astarf.SetHeuristic(&z);
        while (astarf.GetNumOpenItems() > 0)
        {
            astarf.DoSingleSearchStep(p);
        }
            
        for (int x = 0; x < g->GetNumNodes(); x++)
        {
            double dai;
            node *n = g->GetNode(x);
            astarf.GetClosedListGCost(x, dai);
            n->SetLabelF(label+i, dai);
        }
    }
    
    
    std::vector<graphState> rpivots;
    
    
    /*nofcp=28;
    for(int i = 0; i <nofcp; i++){
        for(int j = 0; j <nofcp; j++){
            if(map->GetNodeNum((wi/(nofcp-1))*j,(hi/(nofcp-1))*i)==-1){
                bool flag=false;
                for (int m=3; m<wi; m+=2){
                    if (flag==true)
                        break;
                    int r=(hi/(nofcp-1))*i-m/2;
                    int c=(wi/(nofcp-1))*j-m/2;
                    for (int k=0; k<m; k++){
                        if(flag==true)
                            break;
                        for (int l=0; l<m; l++){
                            if(map->GetNodeNum(c+l, r+k)!=-1){
                                samples.push_back(map->GetNodeNum(c+l,r+k ));
                                flag=true;
                                break;
                            }
                        }
                    }
                }
            }
            else{
                samples.push_back(map->GetNodeNum((wi/(nofcp-1))*j, (hi/(nofcp-1))*i));
            }
        }
        
    }*/
    
    /*for(int j=0;j<sl->GetNumExperiments();j++){
            //int j = rand() % sl->GetNumExperiments();
            Experiment e = sl->GetNthExperiment(j);
            xyLoc start, goal;
            start.x = e.GetStartX();
            start.y = e.GetStartY();
            goal.x = e.GetGoalX();
            goal.y = e.GetGoalY();
            samples.push_back(map->GetNodeNum(start.x, start.y));
            samples.push_back(map->GetNodeNum(goal.x, goal.y));
    }*/
    
    
    double sum=0;
    for(int i = 0; i < nofp; i++){
        sum = ComputeHeuristic(samples, &h);
        cout<<sum<<endl;
        int k=0;
        for(int j = 0; j < pivots.size(); j++){
            DifferentialHeuristic<graphState> h1(g, label+j, 1);
            h.AddHeuristic(&h1);
            double s= ComputeHeuristic(samples, &h);
            if (s>sum){
                sum=s;
                k=j;
            }
            h.RemoveHeuristic();
        }
        rpivots.push_back(pivots[k]);
        DifferentialHeuristic<graphState>* h1= new DifferentialHeuristic<graphState> (g, label+k, 1);
        h.AddHeuristic(h1);
    }
    for(int i = 0; i < nofp; i++){
        //xs.push_back(ge->GetLocation(rpivots[i]).x);
        //ys.push_back(ge->GetLocation(rpivots[i]).y);
        //cout<<"pivot"<<xs[i]<<" "<<ys[i]<<endl;
        cout<<rpivots[i]<<" ";
    }
    
    //Doing Embedding
    //gg = GraphSearchConstants::GetUndirectedGraph(map);
    //gge = new GraphEnvironment(gg);
    //gge->SetDirected(false);
    for(int i = 0; i < nofp; i++){
        astarf.SetStopAfterGoal(false);
        astarf.InitializeSearch(ge, rpivots[i], 0, p);
        astarf.SetHeuristic(&z);
        while (astarf.GetNumOpenItems() > 0)
        {
            astarf.DoSingleSearchStep(p);
        }
        for (int x = 0; x < g->GetNumNodes(); x++)
        {
            double dai;
            node *n = g->GetNode(x);
            astarf.GetClosedListGCost(x, dai);
            n->SetLabelF(label+i, dai);
        }
    }
}

double ComputeHeuristic(std::vector<graphState> samples, Heuristic<graphState> *h){
    double cost=0;
    for(int i = 0; i < samples.size(); i++){
        for(int j=i+1;j<samples.size(); j++){
            cost+=h->HCost(samples[i],samples[j]);
        }
    }
    return cost;
}

double ComputeHeuristicScen(Heuristic<graphState> *h){
    double cost=0;
    /*for(int i = 0; i < samples.size(); i++){
        for(int j=i+1;j<samples.size(); j++){
            cost+=h->HCost(samples[i],samples[j]);
        }
    }
     */
    return cost;
}

void ComputeCapturedHeuristicAtEachDimension(){
    std::vector<graphState> samples;
    int nofp=10;
    int nofd=10;
    int nofmd=5;
    short nofcp=7;
    short nofSamples=400;
    
    // FMn
    DoDimensions(GraphSearchConstants::kFirstData, nofd, 0);
    
    // (n-1)FastMap + DH
    DoDimensions(GraphSearchConstants::kFirstData + nofd, nofd, 1);
    
    int nofh = 2;
    std::vector<FILE*> files;
    cout<<"Computing heuristics"<<endl;
    
    for(int i=0;i<nofh;i++){
        std::string fname = saveDirectory + std::string("/Different_Heuristics/NoE/");
        FILE *f = fopen((fname+mapName+" - h"+ to_string(i) + "- Capt" + ".txt").c_str(), "w+");
        files.push_back(f);
    }

    ResetEdgeWeights(kEdgeWeight+1);
 
    for (int i = 0; i < largestPartNodeNumbers.size(); i++){
        samples.push_back(largestPartNodeNumbers[i]);
    }
    
    for (int i = 0; i<nofh; i++){
        if (i != 1){
            for (int j = 0; j < nofd; j++){
                EmbeddingHeuristic<graphState> hs(g, i * nofd + GraphSearchConstants::kFirstData + j, 1);
                cout<<"h"<<i<<" d"<<j<<" "<<ComputeHeuristic(samples, &hs)<<endl;
                string s = "d" + to_string(j) + " " + to_string(ComputeHeuristic(samples, &hs)) + "\n";
                fputs(s.c_str(), files[i]);
                fflush(files[i]);
            }
        }
        else{
            EmbeddingHeuristic<graphState> hs(g, i * nofd + GraphSearchConstants::kFirstData + 9, 1);
            cout<<"h"<<i<<" d"<<"9"<<" "<<ComputeHeuristic(samples, &hs)<<endl;
            string s = "d" + to_string(9) + " " + to_string(ComputeHeuristic(samples, &hs)) + "\n";
            fputs(s.c_str(), files[i]);
            fflush(files[i]);
        }
        fclose(files[i]);
    }
    
}

void NormalizeGraph()
{
    double maxx=0, maxy=0;
    double minx = me->GetMap()->GetMapWidth()*me->GetMap()->GetMapHeight();
    double miny = me->GetMap()->GetMapWidth()*me->GetMap()->GetMapHeight();
    for (int x = 0; x < g->GetNumNodes(); x++)
    {
        node *n = g->GetNode(x);
        double tx = n->GetLabelF(GraphSearchConstants::kXCoordinate);
        double ty = n->GetLabelF(GraphSearchConstants::kYCoordinate);
        maxx = std::max(maxx, tx);
        minx = std::min(minx, tx);
        maxy = std::max(maxy, ty);
        miny = std::min(miny, ty);
    }
//    printf("X from %f to %f\n", minx, maxx);
//    printf("Y from %f to %f\n", miny, maxy);
    double scale = 2.0/std::max(maxx-minx, maxy-miny);
    double xOff = minx+(maxx-minx)/2;
    double yOff = miny+(maxy-miny)/2;

    for (int x = 0; x < g->GetNumNodes(); x++)
    {
        node *n = g->GetNode(x);
        double tx = n->GetLabelF(GraphSearchConstants::kXCoordinate);
        double ty = n->GetLabelF(GraphSearchConstants::kYCoordinate);
        tx = (tx-xOff)*scale;
        ty = (ty-yOff)*scale;
        n->SetLabelF(GraphSearchConstants::kXCoordinate, tx*0.95);
        n->SetLabelF(GraphSearchConstants::kYCoordinate, ty*0.95);
    }

}

#include "SVGUtil.h"
void MyWindowHandler(unsigned long windowID, tWindowEventType eType)
{
    if (eType == kWindowDestroyed)
    {
        printf("Window %ld destroyed\n", windowID);
        RemoveFrameHandler(MyFrameHandler, windowID, 0);
    }
    else if (eType == kWindowCreated)
    {
        printf("Window %ld created\n", windowID);
        //glClearColor(0.99, 0.99, 0.99, 1.0);
        InstallFrameHandler(MyFrameHandler, windowID, 0);
        ReinitViewports(windowID, {-1.0f, -1.f, 0.f, 1.f}, kScaleToSquare);
        AddViewport(windowID, {0.f, -1.f, 1.f, 1.f}, kScaleToSquare); // kTextView

        CreateMap(kRoomMap8);
    }
}

int frameCnt = 0;

void MyFrameHandler(unsigned long windowID, unsigned int viewport, void *)
{
    Graphics::Display &display = getCurrentContext()->display;
    if (lerp < 1 && recording)
    {
        lerp += lerpspeed;
        if (lerp > 1)
            lerp = 1;
        graphChanged = true;
    }
    else { lerp = 1; }
    
    if ((mapChange||graphChanged) == true && viewport == 0)
    {
        display.StartBackground();
        display.FillRect({-1, -1, 1, 1}, Colors::black);

        me->Draw(display);
//        basege->Draw(display);
        display.EndBackground();
        mapChange = false;
    }
    if (graphChanged == true && viewport == 1)
    {
        display.StartBackground();
        display.FillRect({-1, -1, 1, 1}, Colors::black);

        ge->SetDrawEdgeCosts(false);
        ge->SetColor(Colors::white);
        if (doLerp) {
            ge->DrawLERP(display, base, g, lerp);
        }
        else {
            ge->SetNodeScale(g->GetNumNodes()/20);
            ge->Draw(display);
        }
        display.EndBackground();
        graphChanged = false;
    }
    if (viewport == 0 && nodeToDraw != -1 && stop==0)
    {
        GLdouble x, y, z, r;
        me->GetMap()->GetOpenGLCoord(stateToDraw.x, stateToDraw.y, x, y, z, r);
        Graphics::point p(x, y);
        display.FillCircle(p, 0.03, Colors::red);
        
        // kxcordinate and kycordinate are used for showwing fastmap embeddings. by doing the line below we change the kxcordinate and kycordinate to be locations in map instead of embeding so we can get x and y of a node later. it won't show the point in embedding anymore.
        //StoreMapLocInNodeLabels();
        
        //xs.push_back(ge->GetLocation(5690).x);
        //ys.push_back(ge->GetLocation(5690).y);
        
        for (int i=0; i<xs.size(); i++){
            
            //cout<<i<<endl;
            //cout<<xs[i]<< " "<<ys[i]<<endl;
            /*
            if(i&2==0)
                me->GetMap()->GetOpenGLCoord(xs[iindex[i/2]*2], ys[iindex[i/2]*2], x, y, z, r);
            else
                me->GetMap()->GetOpenGLCoord(xs[iindex[(i-1)/2]*2+1], ys[iindex[(i-1)/2]*2+1], x, y, z, r);
            */
            me->GetMap()->GetOpenGLCoord(xs[i], ys[i], x, y, z, r);
            p.x=x;
            p.y=y;
            //if(i<10)
            //cout<<x<<" "<<y<<endl;
            display.FillCircle(p, 0.02, Colors::yellow);
            /*
            if (i/2<1)
                display.FillCircle(p, 0.02, Colors::red);
            else if (i/2<2)
                display.FillCircle(p, 0.02, Colors::blue);
            else if (i/2<3)
                display.FillCircle(p, 0.02, Colors::black);
            else if (i/2<4)
                display.FillCircle(p, 0.02, Colors::yellow);
            else
                display.FillCircle(p, 0.02, Colors::purple);
            //else
            //    display.FillCircle(p, 0.02, Colors::purple);
            */
            
        }
        //cout<<"........"<<endl;
        //stop=1;
        
    }
    if (viewport == 1 && nodeToDraw != -1)
    {
        node *n = g->GetNode(nodeToDraw);
        Graphics::point p(n->GetLabelF(GraphSearchConstants::kXCoordinate),
                          n->GetLabelF(GraphSearchConstants::kYCoordinate));
        display.FillCircle(p, 0.03, Colors::red);
        
    }
    if (viewport ==1 && recording)
    {
        SaveSVG();
        recording=false;
    }
}

int MyCLHandler(char *argument[], int maxNumArgs)
{
    if (strcmp( argument[0], "-map" ) == 0 )
    {
        if (maxNumArgs <= 1)
            return 0;
        strncpy(gDefaultMap, argument[1], 1024);
        strncpy(scenfile, argument[2], 1024);
        cout<<gDefaultMap<<endl;
        //Extracting the name of the map
        for(int i=strlen(gDefaultMap);i>=0;i--)
            if(gDefaultMap[i]=='/'){
                strncpy(mapName, gDefaultMap+i+1, strlen(gDefaultMap)-1-i-4);
                break;
            }
        strncpy(saveDirectory, argument[3], 1024);
        return 2;
    }
    // Not sure about this
    return 0;
}

void MyDisplayHandler(unsigned long windowID, tKeyboardModifier mod, char key)
{
    switch (key)
    {
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
            CreateMap((mapType)(key-'0'));
            break;
        case 'l':
            lerp = 0;
        case '[':
        {
//            stepsPerFrame /= 2;
//            std::string s = std::to_string(stepsPerFrame)+" steps per frame";
//            submitTextToBuffer(s.c_str());
        }
            break;
        case ']':
        {
//            if (stepsPerFrame <= 16384)
//                stepsPerFrame *= 2;
//            if (stepsPerFrame == 0)
//                stepsPerFrame = 1;
//            std::string t = std::to_string(stepsPerFrame)+" steps per frame";
//            submitTextToBuffer(t.c_str());
        }
            break;
        case '|':
//            h.Clear();
            break;
        case 'a':
////            submitTextToBuffer("Click anywhere in the map to place a differential heuristic");
//            m = kAddDH;
            break;
        case 'm':
//            if (h.values.size() == 0)
//            {
//                submitTextToBuffer("Error: Must place DH first");
//                break;
//            }
//            m = kMeasureHeuristic;
            break;
        case 'd':
//            showDH = !showDH;
//            mapChanged = true;
            break;
        case 'p':
        {
//            if (showDH)
//            {
//                showDH = false;
//                mapChanged = true;
//            }
//            m = kFindPath;
//            start = goal = {0, 0};
//            path.resize(0);
//            submitTextToBuffer("Click and drag to find path");
        }
            break;
        case 'h':
//            if (h.values.size() == 0)
//            {
//                submitTextToBuffer("Error: Must place DH first");
//                break;
//            }
//            submitTextToBuffer("Select two of the points that have a high heuristic value in the current DH");
//            m = kIdentifyHighHeuristic;
//            FindSamplePoints();
            break;
        case 'r':
            recording=true;
            break;
    }
}


void GetMapLoc(tMouseEventType mType, point3d loc)
{
    int x, y;
    me->GetMap()->GetPointFromCoordinate(loc, x, y);
    nodeToDraw = me->GetMap()->GetNodeNum(x, y);
    stateToDraw.x = x;
    stateToDraw.y = y;
    printf("Hit (%d, %d)\n", x, y);
    stop=0;
    /*if(map->GetNodeNum(stateToDraw.x,stateToDraw.y)!=-1){
        TemplateAStar<graphState, graphMove, GraphEnvironment> astarf;
        std::vector<graphState> pa;
        ZeroHeuristic<graphState> zh;
        GraphMapHeuristicE<graphState> h0(map, g);
        GraphHeuristicContainerE <graphState> h(g);
        h.AddHeuristic(&h0);
        astarf.SetStopAfterGoal(false);
        astarf.InitializeSearch(ge, map->GetNodeNum(xs[49],ys[49]), map->GetNodeNum(stateToDraw.x,stateToDraw.y), pa);
        astarf.SetHeuristic(&zh);
        double gc=0;
        while (astarf.GetNumOpenItems() > 0)
            astarf.DoSingleSearchStep(pa);
        astarf.GetClosedListGCost(map->GetNodeNum(stateToDraw.x,stateToDraw.y), gc);
        cout<<"H Error is: "<<gc-h.HCost( map->GetNodeNum(xs[49],ys[49]), map->GetNodeNum(stateToDraw.x,stateToDraw.y))<<" between"<<xs[49]<<" "<<ys[49]<<", "<<stateToDraw.x<<" "<<stateToDraw.y<<endl;
        xs[49]=stateToDraw.x;
        ys[49]=stateToDraw.y;
    }*/
}

double dist(point3d loc, graphState s)
{
    double x = g->GetNode(s)->GetLabelF(GraphSearchConstants::kXCoordinate);
    double y = g->GetNode(s)->GetLabelF(GraphSearchConstants::kYCoordinate);
    return (loc.x-x)*(loc.x-x)+(loc.y-y)*(loc.y-y);
}

void GetGraphLoc(tMouseEventType mType, point3d loc)
{
    graphState best = 0;
    for (int t = 0; t < g->GetNumNodes(); t++)
        if (dist(loc, t) < dist(loc, best))
            best = t;

    nodeToDraw = best;
    stateToDraw.x = g->GetNode(best)->GetLabelL(GraphSearchConstants::kMapX);
    stateToDraw.y = g->GetNode(best)->GetLabelL(GraphSearchConstants::kMapY);
}

bool MyClickHandler(unsigned long , int viewport, int windowX, int windowY, point3d loc, tButtonType button, tMouseEventType mType)
{
    if (viewport == 0)
        GetMapLoc(mType, loc);
    if (viewport == 1)
        GetGraphLoc(mType, loc);
    return true;
}

void SaveSVG()
{
    cout<<"saving svgs"<<endl;
    //svg for FM2 and FM1+DH
    Graphics::Display d;
    std::string fname = saveDirectory + std::string("/SVG/");
    
    //FM2
    d.FillRect({-1, -1, 1, 1}, Colors::white);
    DoDimensions(GraphSearchConstants::kFirstData, 2, 0);
    for (int x = 0; x < g->GetNumNodes(); x++)
    {
        node *n = g->GetNode(x);
        n->SetLabelF(GraphSearchConstants::kXCoordinate, n->GetLabelF(GraphSearchConstants::kFirstData));
        n->SetLabelF(GraphSearchConstants::kYCoordinate, n->GetLabelF(GraphSearchConstants::kFirstData+1));
    }
    NormalizeGraph();
    ge->SetNodeScale(g->GetNumNodes()/20);
    ge->SetColor(Colors::black);
    ge->Draw(d);

    printf("Save to '%s'\n", (fname+mapName+"-FM2"+".svg").c_str());
    MakeSVG(d, (fname+mapName+"-FM2"+".svg").c_str(), 800, 800, 0);
    
    
    // line
    /*
    DoLineH(GraphSearchConstants::kFirstData, 5, kSub5);
    for (int x = 0; x < g->GetNumNodes(); x++)
    {
        node *n = g->GetNode(x);
        n->SetLabelF(GraphSearchConstants::kXCoordinate, n->GetLabelF(GraphSearchConstants::kFirstData));
        n->SetLabelF(GraphSearchConstants::kYCoordinate, n->GetLabelF(GraphSearchConstants::kFirstData+1));
    }
    NormalizeGraph();
    ge->SetNodeScale(g->GetNumNodes()/20);
    ge->SetColor(Colors::black);
    ge->Draw(d);
    fname = saveDirectory + std::string("/SVG/");
    printf("Save to '%s'\n", (fname+mapName+"-FM2"+".svg").c_str());
    MakeSVG(d, (fname+mapName+"-FM2"+".svg").c_str(), 800, 800, 0);
    */
    
    
    //FM1+DH
    /*
    d.FillRect({-1, -1, 1, 1}, Colors::white);
    DoDimensions(GraphSearchConstants::kFirstData+2, 2, 1);
    for (int x = 0; x < g->GetNumNodes(); x++)
    {
        node *n = g->GetNode(x);
        n->SetLabelF(GraphSearchConstants::kXCoordinate, n->GetLabelF(GraphSearchConstants::kFirstData+2));
        n->SetLabelF(GraphSearchConstants::kYCoordinate, n->GetLabelF(GraphSearchConstants::kFirstData+3));
    }
    NormalizeGraph();
    ge = new GraphEnvironment(g);
    ge->SetNodeScale(g->GetNumNodes()/20);
    ge->SetColor(Colors::black);
    ge->Draw(d);
    printf("Save to '%s'\n", (fname+mapName+"-FM1+DH1"+".svg").c_str());
    MakeSVG(d, (fname+mapName+"-FM1+DH1"+".svg").c_str(), 800, 800, 0);
    */
}

void LoadMap(Map *m)
{
    m->Scale(194, 205);
    const char map[] = "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@T@@@@@@@@@@@@@@@@@TTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@T@@@@@@@@@@@@@@@@@TTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@T@@@@@@@@@@@@@@@@@@T@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@T@@@@@@@@@@@@@@@@@@T@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTT@@@@@@@@@@@@@@@@@T@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTT@@@@@@@@@@@@@@@@TTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@T@@@@@@@@@@@@@@@@@TTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@T@@@@@@@@@@@@@@@@@TTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@T@@@@@@@@@@@@@@@@@@TTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@T@@@@@@@@@@@@@@@@@@TTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTTTTT.TTTT..TTT...TTTTTTTT.....TTTTT......TTTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTT.TTTT.TTTT..TTT...TTTTTTTT.....TTTTT..TTT...TT@@@TTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTTTT.TTTT...T...TTT...TTTTTTTT.....TTTTTTTTTT...TT@@@TTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTT...TTTT...TTTT.......TTT...TTTTTTTT.....TTTTTTTTTTT..TTTTTTTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTT..........TTTT..TTT..TTT...TTTTTTTT.....TTTTTT.TTTT..TTTTT.TTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTT............TTTTTTT..T....TTTTTTTT.....TT.TT...TTT...TTT...TTTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTTT............TTTTTTT..T....TTTTTTTT.....TT....TTTTTT..........TTTTT.TT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTTTT..TTT......TTTT......T....TTTTTTTT.....T.....TTTTTT.....T.....TTT....T@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TT..TTTTTT....TTTTTT..TTTTT......T....TTTTTTTT.....T......TTT.....TTTT.............T@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTT....TTT......TTTTTT..TT........TTT...TTTTTTTT.....T.........TT...TTTT..............TT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTT......T.......TTTTTTT............TTT...............TTT........TT....TTTT...............TT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTT.............................TT....T................TTT........TT....TTTT................TT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTT.TTT........................TTT....T................TTT....TT...T.....TTT...............TTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTTTTT.......................TTTT....T.................T.....TT.........TT.............TT.TTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTTTTTTTT......................TTTT....T.................T.....TTT......................TTTTTTTTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TT.TTTTTTTTTTTT.....TTT.....................TTT................T.....TT......................TTTTTT@TTTTTT.T@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTT...TTTT@TTTTTTT....TTTT...................TTTT................T.............................TTTTT@TTTTTT...TT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTT....TTTTT@TTTTTTT....TTTT...................TTTT...............TTT......................TT...TTTTTT@TT.......TTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTT......TTTTTT@TTTTTTT....TTTT..................TTTT...............TTTT.....................TTTTTTTTTT@TTTT......TT@@T@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TT.........TTTTT@TTTTTT..TTTTTT...................TTTT..............TTTTT.....................TTTTTTTTTT@TTTT......TTT@@@T@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTT...........TTTTT@TTT....TTTTT...................TTTT...............TTTTT.....................TTTT@@TTT@TTTTT........TTT@TTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTT...............TTTTTTT..TTTTT.......................T.................TTTT..TT................TTTTT@@TTTTTTTT...........TTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTT.....TT..........TTTT@TT..TTTTT..........................................TTT....................TTTTT@@TT@TTTTTT...........TTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTT......TTTT.........TTTT@TTTTTTTT..........................................TT.....................TTTTT@TT@TTTTTTT............TTTTT@TT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTT......TTTTTTT.TT......TT@TTTT.TT....................TT............................................TTTT@@TT@TTTTTTT............TTTTTT.TTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTT......TTTTTTTTTTT...TTTTT@TTT.......................TTT..........................................TTTTT@TT@TTTTTTTT.............TTT...TTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTT......TTTTTTTTTTTTTTTTTTT@TTT.......................TTT.......................TT.................TTTTTTTT@TT.TTTTT..............T....TTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTT...TTTTTTTTTT.TTTTTTTTTTTT@TT................................................TTT.................TTTT@TT@TT..TTT........................TT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTT...TTTTTTTTTT...TTTTTTTTTTTTTTT................................................TT................TTTTTTT@TTT.TTTTTT.....................TTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTT....TTTTTTTTTT.....TTTTTTTTTTT@TT...TT.............................................................TTTTTTT@TT..TTTTT......................TTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTT......TTTTTTTTT.....TTTTTTTTTTTTT@TT..TTT..............T............TTTTTTT..........................TTTTTT@TTT....TTT........................TTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TT........TTTTTTTTT.....TTTTTTTTTTTTT@TTT.TTT............TTTT.........TTTTTTTTT..........................TTTTTT@TT................................TTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@T..........TTTTTTTT.........TTTT.TTTTT@TT.TTT............TTTT.........TTTTTTTTTT..........T....TTTT..TTTTTTTTT@TTT...........................T....TTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TT...........TTTTTTT..........T....TTTT@TTT....TT.......TTTTTT......TTTTTTTTTTTTTT........TTTT.TTTTTTTTTTTTTTTT@TT.................................TTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@T............TTTT.................TTTTTT@TT....TTTT.....TTTTTT......TTTTTTTTTTTTTT........TTTTT@TTT@TTTTTTTTTT@TT..................................TT..T@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TT................................TTTTTTTTTTT..TT@TT...TTTTT.........TTTTTTTTTTTTTTT......TTTTTT@@@@@@TTT@TTTT@TTT........................T...T.........TT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@T................................TTTTTTTTT@TTTTTTT@@TTTTT............TTTTTTTTTTTTTTT.......TTTTTTT@@@@@@@TTTTT@TT........................................T@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTT...............................TTTTTTTTTT@TTTTT@@@TTT..............TTTTTTTTTTTTTTT.............TTTTT@@@TTTTTTTT........................................TT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTT................................TTTTTTTTT@TTTT@@TTTTT..............TT.TTTTTTTTTT.................TTT@@@@TTTTTT..........................................T@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@T@TT................................TTTTTTTTTTTTTTTTT......................TTTTTTTTT..................TTTTTTTTTTTT........................................TTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TT@TTTT............................TTTTTTTTTTTTTTTTTT........................TTTTTTTT.....................TTTTTTTT.......................................TTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TT@TTTT............................TTTTTTT@TTTTTTTTT.........................T.............................TTTTTTTT......................................TTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTTT.TTT..........................TTTT@@TTTTTTTT.......................................................TTTTTTTT...............TTT.....................TTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTTTTTTT..........................TTTTTTTTTTTTTT.......................................................TTTTTTTTT...........TTTTTTT.......................TTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTTTTTTTTTT........................TTTTTTTTTTTTTT......................................................TTTTTTTTTT.........TTTTTTTTT........................TTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTTTTTTTTTTT............................TTTTTTT.........................................................TT@TT.TTTT........TTTTTTTTTT.........................TTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTTTTTTTTTTT...........................TTTTTTT.........................................................TT@TT..TT..........TTTTTTTTT.........................TTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTTTTTTTTTT............................TTTTTTT........................................TT................TT@TT..............TTTT..............................TTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTTTTTTT.TT...........................TTTTTTT....................................TT...TTT..............TT@TT..............TTTT....TTT........................TTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTTTTTTTT.............................TTTTTTT....................................TTTTTTTTT....TTT......TTT@TT..............TTTTTT.TTTTT.........................TTTTT@@@@@@@@@@@@@@@@@@@@@@@@TTTTT@@TTTTTTT.............................TTTTTT.................................TTTTTTTTTTTTT....TTTT.....TT@TT.............TTTTTTTTTTTTTTT........................TTTTT@@@@@@@@@@@@@@@@@@@@@@@T@TTTTTT@@TTTT...............................TTTT..................................TTTTTTTTTTTT.....TTT.....TT@TTT............TTTTTTTTTTTTTTTTT........................TTTTT@@@@@@@@@@@@@@@@@@@@@TTTTTTTTTTT@TTTTT..............TTTT............TT............TT..................TTTTTTTTTTTTTTT.....TTT.....TT@TT.............TTTTTTTTTTTTTTTTTT...........................TT@@@@@@@@@@@@@@@@@@@@TTTTTTTTTTTTT@TTTT..TTT.....TTTTTTT.........................TTTT.............TTTTTTTTTTTTTTTTTTT....TTTTTT..TT@TT..............TTTTT@TTTTTTTTTTTT..........TT................T@@@@@@@@@@@@@@@@@@@TTTTTTTTTT@TTTT@@TTTTTTT...TTTTTTTTT.TTT.TTT.................TTTT.........TTTTTTTTTTTTTTTTTTTTTTT....TTTTTT..TT@TT...............TTTT@@@TTTTTTTTTT........TTTTT...............TT@@@@@@@@@@@@@@@@@@TTTTTTTTTT@@@TTTT@@TTTTT..TTTTTTTTTTTTTTTTTTT................TTTT.....TTTTTTTTTTTTTTTTTTTTTTTTTT.....TTTTT..TT@TT...................TTTT@TTTTTTTTT.....TTTTTTTT.............TTTT@@@@@@@@@@@@@@@@@TTTTTTTTTTTTT@TTTTTT@TTTTT..TTTTTTTTTTTTTTTTTTT..............TTTTTTTTTTTTTTTTTTTTT@@@@@TTTTTTTTTT....TTTTTTTTTTTTT....................TTTT@TTTTTTTTT...TTTTTTTTTT............TTTTT@@@@@@@@@@@@@@@TTTTTTTTTTTTTTTTTTTTTTT@TTTT.TTTTTTTT@@@TTTTTTT..............TTTTTTTTTTTTTTTTTT@@@@@@@@@TTTTTTTTTT....TTTTTTTTTTTT....................TTTTTT@TTTTTTTT...TTTTTTTTT..............TTTT@@@@@@@@@@@@@@@TTTTTTTT@TTTTTTTTTT.TTTT@@TTTTTTTTTTT@@TTTTTTTTT.............TTTTTTTTTTTTTTT@@@@@@@@@@@@@@TTTTTTTT....TTTTTTTTTTTTT...TT..............TTTTT@TTTTTTTTTT.TTTTTTTTTT...............TTTT@@@@@@@@@@@@@TT..TTT..TTTTTTTTT.....TTTT@@TTTTTTTT@@TTTTTTTTTT.............TTTTTTTTTTT@@@@@@@@@@@@@@@@@@@TTTTTT.....TTTTTTTTTTTTT..................TTTTTTTTTTTTTTT@@TTTTTTTTTTT...............TTTTT@@@@@@@@@@@@T....T...TTTTTTTTT.......TTTT@TTTTTTT@TTTTTTTTTTT............TTTTTTTTTTTT@@@@@@@@@@@@@@@@@@@TTTTTT....TTTTTTTTTTTTTTT.................TTT@@@TTTTTTTTTTTTTTTTTTTTTT................TTTT@@@@@@@@@@@TT........TTTTTTTTT.....TTTTTTT@@TTTTTTTTTTTTTTTTT.............TTTTTTTTTTT@@@@@@@@@@@@@@@@@@@TTT@TT....TTTTTTTTTTTTTTTT...............TT@@@@@TTTTTTTTTTTTTTTTTTTT.T................TTTTT@@@@@@@@@@T..........TTTTTTT......TTT@@TTTT@@TTTTTTTTTTTTTT...............TTTTTTTTTT@@@@@@@@@@@@@@@@@@@TT@@@.....TTTTTTTTTTTTTTTTT............TTT@@@@@@@TTTTTTTTTTTTT.TT....TT..............TTTTTT@@@@@@@@@T............TTTTT.....TT.TTT@@@TTTTTTTTTTTTTTTTTT...............TTTTTTTTTTT@@@@@@@@@@@@@@@@@TTT@@@@@@@.TTTTTTTTTTTTTTTTT...........TTT@@@@@@@@TTTTTTTTTTTTT.......TTT.............TTTTTTT@@@@@@@TT.............TTT.....TTT....TTTTTTTTTTTTTT.....TT...............TTTTT.TTTTT@@@@@@@@@@@@@@@@@TT@@@@@@@@@@@@TTTTTTTTTTTTTT...........TTT@@@@@@@@TTTTTTTTTTTTT.......TT...............TTTTTTT@@@@@@TT....................TTTT....TTTTTTTTTTTTTT.....TT................TTT..TTTTTT@@@@@@@@@@@@@@@@TTTT@@@@@@@@@@TTTTTTTT..TTT...........TT@@@@@@@@@@TTTTTTTTTTTT.........TT...............TTTTTT@@@@@TTT....................TTTT....TTTTTTTTTTTTT.............TT..........T....TTTTTT@@@@@@@@@@@@@@@TTTTTTTT@@@@@@TTTT.......T...........TT@@@@@@@@@@@@TTTTTTTTTTT.........TT................TT.TTT@@@@TTT....................TTTT....TTTTTTTTTTTTT............TTTTT.TT...........TTTTT@@@@@@@@@@@@@@@TTTTTTTTTTTT@TTTT..............TT..TTT@@@@@@@@@@@@@TTTTTTTTTTT..........T...................TTT@@@TTTT......................T.....TTTTTTTTTTT..............TTTTTTTTT..........TTTTTT@@@@@@@@@@@@@TTTTTT@@TTTTTTTTTT.............TTTTTTTT@@@@@@@@@@@@@TTTTTT..TTT..........TTT.................TTTT@TTTTT............................TTTTTTTTTTT..............TTTTTTTTTT..........TTTTT@@@@@@@TTTTTTTTTTTT@@@@@@TTTTTT............TTTTTTTT@@@@@@@@@@@@@@@T@@T...............TTTTT................TTTT@TTTTT...........................TTTTTTTTTTTT...............TTTTTTTTT...........TTTTT@@@@@@TTTT@@TTTTTT@@@@@@@TTTT.............TTTTTTTT@@@@@@@@@@@@@@@@@@TTTTT...........TTTTT..................TTTTTTTT...........................TTTT...TTTTTTTT...........TTTTTTTTT............TTTTTTTTTTTTTT@TTTTTTTT@@@@@TTTTT...............TTTTTTTTT@@@@@@@@@@@@@@@@TTTTT...........TTTTT...................TTTTTTT............................TTT..TTTTTTTTT..........TTTTTTTTT..............TTTTTTT@@@@@TTTTTTTTTTTTTTTTTTT.................TTTTTTTTT@@@@@@@@@@@@@@@TTTTT............TTTT.................TTTTTTTTT.............................TT..TTTTTTTTT..........TTTTTTTT...............TTTTTTT@@@TTTTTTTTTTTTTTTTTTT...................TTTTTTTTT@@@@@@@@@@@@@@@TT....................................TTTTTTT...............................TT..TTTTTTTT...........TTTTTTTTT...............TTTTT@@@TTTTTTTTTTTTTT.......................TTTTTTTTTTT@@@@@@@@@@@@@@@@T..................................TT@@TTT........TTTT.........................TTTTT..T...........TTTTTTTTTTT...............TTTTT@TTTTTTTTTTTTT......................TTTTTTTTTTTTT@@@@@@@@@@@@@@@@TT.................T...............TT@@TTT........TTTT.........................TTTT...............TTTTTTTTTTTT..................TTTTTTTT..TTTTT.....................TTTTTTTTTTTTTT@@@@@@@@@@@@@@@@TT......TTT.......T................TT@@@TT........TTTT.TT.......................TT................TTTTTTTTTTTTTT...................TTT......TT....................TTTTTTTTTTTTTTTT@@@@@@@@@@@@@@@@@TT..TTTTTT........................TT@@@TT........TTTTTTT.......................................TTTTTT@@@TTTTTTTTT..............................................TTTTTTTTTT@@@@TTTT@@@@@@@@@@@@@@@@TTTTTTTTTT.........................TT@@@TT........TTTTTTTT......................................TTTTT@@@@@@TTTTTTTT.............................................TTTTTTTTT@@@@@@TTTT@@@@@@@@@@@@@@@TTTTTTT............................TT@@@TT........TTTTTTTT......................................TTTTT@@@@@@@TTTTTTTT...........................................TTTTTTTT@@@@@@@@TTTT@@@@@@@@@@@@@TTTTTTT..............................TT@@@TT..............T....................TT.................TTTTT@@@@@@@@@TTTTTT...........................................TTTTTT@@@@@@@@@@TTTT@@@@@@@@@@@@TTTTTTT............................T..TT@@@TT.................TT............TTT.TTTT...............TTTT@@@@@@@@@@@@TTTT..........................................TTTTT@@@@@@@@@@@@TTTT@@@@@@@TTTTTTTTTTT..........................TTTTT.TT@@@TT.................TT............TTTTTTTT...............TTTT@@@@@@@@@@@@TTT...........................................TTTTT@@@@@@@@@@@@@TTTTTT@TTTTTT.................................TTTTTT.TT@@@TT........TTT......TTT.......TTTTTTTTTTTT...............TTTT@@@@@@@@@@@@TTT..............................................T@@@@@@@@@@@@@@TTTTTTTT.....................................TTTTTTT.TT@@@TT........TTT......TTT.....TTTTTTTTTTTTTT...............TTTT@@@@@@@@@@@TTTTT.............................................T@@@@@@@@@@@@@@TTTTTT......................................TTTTTT...TT@@@TT........TTT.TTTTTTT.....TTTTTTTTTTTTTTTTTTT...........TTT@@@@@@@@@@@@TTTTT.............................................TT@@@@@@@@@@@@@@TTTTT.....................................TTTT.TT...TT@@@TTTTT.........TTTTTTT....TTTTTTTTTTTTTTTTTTTT.........TTTTT@@@@@@@@@@@TTTTTT.........................TTT..................TT@@@@@@@@@@@@@TTTT@T....................................TTTT......TT@@@TTTTTTTT......TTTTTTT....TTTTTTTTTTTTTTTTTTT..........TTTTT@@@@@@@@@@@TTTTTT........................TTTT.TTT.............TTT@@@@@@@@@@@@@TTTTTTTT..................................TTTTT.....TT@@@TTTTTTTT......TTTTTTT.....TTTTTTTTTTTTTTTTTTT.........TTTTT@@@@@@@@@@@TTTTTT.................TTTTTTTTTTTTTT..............TTT@@@@@@@@@@@@@TTTTTTTT.................................TTT.T......TT@@@TTTTTTTT......TTTTTTTTTT..TTTTTTTTTTTTTT@TTTT.........TTTT@@@@@@@@@@@@TTTTTT.................TTTTTTTTTTTTTT...............TT@@@@@@@@@@@@@@TTTTTTT...........................................TTT@@@TTTTTTTTT.TTT.TTTTTTTTTTT..TTTTTTTTTTTT@@TTTTT........TTTT@@@@@@@@@@@@T@TTTT................TTTTTTTTTTTTTTT................T@@@@@@@@@@@@@@TTTTTT..........................................TTTT@@@@TTTTTTTTTTTTT.TTTTTTTTTTT...TTTTTTTTTTT@@TTT@T........TTTT@@@@@@@@@@@@T@TTTTT...............TTTTTTTTTTTTTT.................T@@@@@@@@@@@@@@TTTT@T..............TTTT...............TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT@@@@@@TT...TTTTTTTTT@@@@@@@@@TTT@TTTTT..............TTTTTTTTTTTTTT..................TT@@@@@@@@@@@@TTTTTTTTTT......TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT@@@@@@@@@TTT@TTTTT..............TTTTTTTTTTTTTT..................TTTTTTTTTTTT@@TTTTTTTTTTT....TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT@@@@@TTTTTTTTTTTTTTTTTTTTTTT@@@@@TTTTTTTTTTTTTTT@@@@@@@@@TTT@TTTTT.............TTTTTTTTTTTTTTT.................TTTTTTTTTTTTTTTTTTTTTTTTTT....TTTTTTTTTTTT@@@@@@@@T@@@@@@@@@@@@@@@@@TTTT@TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT@@@@@@@@@TTT@TTTTT............TTTTTTTTTTTTTTT..................TTTTTTTTTTTTTTTTTTTTTTTTTT....TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT@@@@@@@@@@TT@@TTTT............TTTTTTTTTTTTTTT...................TTTTTTTTT@@TTTTTTTTTTTTTT....TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT@@@@@TT@@TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT@@@@@@@@@@@@T@@TTTT............TTTTTTTTTTTTTTT....................TTTTTTTTTTTTT@TTTTT...............TTTTTTTTTTTTTTTTTTT.......TT@@@@TTTTTT@@@@@@@TTT@@TTTTTTTTTTTTTTTTTTTTTTTTTT@TTTTTTTTTTTTTTTTT@@@@@@@@@@@@T@@TTTTT............TTTTTTTTTTTTTTT....................T@TTTTTTTTTTTTTTT.................TTTTTTT...TTTTTTTT.......TT@@@@@@@@TT@@@@@@@TTT@@TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT@@@@@@@@@@@@TT@TTTT..............TTTTTTTTTTTTTT.....................TTTTTTTTTTTTTTTT.................TTTTTTTT..TTTTTTTT......TTT@@@@@@@@TT@@@@TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT.....TTTTTTTTTTTTTT@@@@@@@@@@@@TTTTTTTT..............TTTTTTTTTTT.......................TTTTT@@TTTTTTTTT...................TTTTTT...TTTTTTT......TT@@@@@@@@@TT@@@@TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT...T..TTTTTTTTTTTTTTT@@@@@@@@@@@TTTTTTTT.............TTTTTTTTTTT.......................TTTTTT@@TTTTTTTTT....................TTTTT....TTTTTT......TT@@@@@@@@@TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT.TTT...TTTT...TTTTTTTTTTTTT@@@@@@@@@@@TTT@TTTT.............TTTTTTTTTTT.................TTT..TTTTTT@@TTTTTTTTT......................TTT......TTTTT......TTT@@@@@@@@TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT.......TTTT....TTTTTTTTTTTT@@@@@@@@@@@@TT@TTTT...............TTTTTTTTT...............TTTTT..TTTTTT@TTTTTTTTT........................TTTTTT..............TT@@@@@@@@TTTTTTTTTTTT...TTTT.....................TTTT.....TT@TTTTTTTT@@@@@@@@@@@@T@TTTTT.................TTTTTT..............TTTTTTTTTTTTT@TTTTTTT............................TTTTTTTTT..........TTT@@@@@@@TT.......................................TTT......T@TTTTTTTT@@@@@@@@@@@@T@TTTT..................TTTTTT.............TTTTTTTTTTTTT@TTTTTTTT...........................TTTT@TTTTTTT........TTT@@@@@@@TT........................................T.......TTTTTTTTTT@@@@@@@@@@@@TTT@TT...................................TTTTTTTTTTTTTTTTTTTTTTTT...........................TTTTTTTTTTTT.........TTT@@@@@@TT................................................TTTTTTTTTT@@@@@@@@@@@@TTTTT...................................TTTTTT@TTTTT@@TTTTTTTTTTT...............................TTTTTTTT.........TTTT@@@@@TTTTTTTTTTTTTT.....................................TTTTTTTTTT@@@@@@@@@@TTTTTT.................................TTTTTTTTTTTTTTTT@TTTTTTTTTTTT................................TTTT...........TTTTT@@@TTTTTTTTTTTTTT...................................TTT@TTTTTTTT@@@@@@@@@TTTTTTTTT..............................TTTTTTT@TTTTTTTTTTTTTTTTTTTTTTT................................................TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT.................TTTTTTTTTTTT@@@@@@@TTTTTTTT@TTT...........................TTTTTTTTTT@@TTT@TTTTTTTTTTTTTTTTT...........................................T.T...TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT.................TTTTTTTTTTTT@@@@@TTTTTTTTTTTTTTTT........................TTTTTTTTTT@@TT@@@@@TTTTTTTTTTTTTT.........................................T..T.T.......TTT@TTTTTTTTTTTTTTTTTTTTTTTTTTTTT..................TTTTTTTTTTTTT@@TTTTTTTTTTTTTTTTTTTT....................TTTTTTTTTT@TTT@@@@@@@@TTTTTTTTTTTTTT.......................T.............T....TTT........TTTTTTTTTTTTTTTTTTTTT..............................TTTTTTTTTTTTTTTTTTTTTTTTTTTTTT@TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT@@@@@@@@@@TTTTTTTTTTTTTTT....................T@T.................TTT........TTTTTTTTTTTTTTTTTTTTT...............................TTTTTTTTTTTTTTTTTTTTTTTTTTTTT@TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT@@@@@@@@@@@@TTTTTTTTTTTTT....................TT.................TTTT........TTTTTTTTTTTTTTTTTT.TT...............................TTTTTTTTTTTTTTTTTTTTTTTTTTTTT@TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT@@@@@@@@@@@@@@TTTTTTTTTTT..............TTTTT.....................TTT@T......TTTTTTTTTTTTTTTTTT...................................TTTTTTTTTTTTTTTT@@TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT@TTTTTTTT@@@@@@@@@@@@@@@@TTTTTTTTTTT..............TTTTTT....................TTTTTT.....TTTTTTTTTTTTTTTTT..........................TT........TT@TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT@@@TTTT@@@@@@@@@@@@@@@@@TTTTTTTTTT..............TTTTTTT.....TT.............TTTTTT...TTTTTTTTTTTTTTTTTTT..........................TT..........TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT@@@@@@@@@@@@@@@@@@@@@@@TTTT@@@@@@@@@@@@@@@@@TTTTTTTTTTT..............TTTTTTT....TTTTT............TTTT....TTTTTTTTTTTTTTTTTTT........................TTTTT..........TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT@@@@@@@@@@@@@@@@@T@TTTTTT@@@@@@@@@@@@@@@@TTTTT..TTTT..................TTT.....TTTTTTT...TT...............TTTTTTTTTTTTTTT.........................TTTTTTTTTTTT....TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT@@@@@@@@@@@TTTTTT@TT@@@@@@@@@@@@@@@@TTTTT.....TT..........................TTTTTTT..TTTT.......TT....TTT@TTTTTTTT.............................TTTTTTTTTTTT......TTTTTTTTTTTTTTTTTTTTTTTTTTT@@@@@TTTTTT@TTT@TTTTTT@@@@@@@@@@@@@@@@@@@@@TTTTT...................................TTTTT..TT@@TTT....TTT..TTTT@@TTTTTTTT.............................TTTTTTTTTTTTT......TTTTTTTTTTTTTTTTTTTTTTTTTT@@@@@@@@@@TTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@TTTTT...................TTTT...............TTT.TT@@@@TT....TT..TTTTT@@@TTTTTTT............................TTTTTTTTTTTTTT.......TTTTTTTTTTTTTTTTTTTTTTTT@@@@@@@@@@@@TTTTT@@@@@@@@@@@@@@@@@@@@@@@@@TTTTT...................TTTTTTTTT............TTTT@@@@@@TT........TTT@@@@@TTTTT....................TT.......TTTTTTTTTTTTT.........TTTTTTTTTTTTTTTTTTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTT...................TTTTTTTTTTTT.........TT@TTTTTTTTTT........TT@@@@@@TTTTT.............................TTTTTTTTTTTTT..........TTTTTTTTTTTTTTTTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTT....................TTTTTTT@TTTTT.........T@TTTTT.............TT@@@@@@@TTTT.............................TTTTTTTTTTTTT...........TTTTTTTTTTTTTTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTT.....................TTTTTTTT@@TTTTT.......TTTTTT..............T@@@@@@@@TT........................TTT...TTTTTTTTTTTTTTT...........TTTTTTTTTTTTTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTT.....................TTTTTTTTT@@@@TTTTTTTTT..TT.......TTTTTT...TT@@@@@@@@@T......................TTTTT.TTTT@TTTTTTTTTTTT............TTTTTTTTTTTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTT.....................TTTTTTTTTTTT@@@@TTTT@TTTT......TTTTTTTTTTTTT@@@@@@@@@@@T...TT...............TTTTTTTTT@@TTTTTTTTTTTT..............TTTTTTTTTTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTTT.....................TTTT@@@@@@TT@@@@@TTT@@@TTTT...TTTTT@@@@TTTT@@@@@@@@@@@@TT.TTTTT...........TTTTTTTTT@@TTTTTTTTT@TTT.................TTTTTTTTTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTTTT....................TTT@@@@@@@@@T@TT@@TTT@@@@TT..TTT@@@@@@@@@TT@@@@@@@@@@@@@T.TTTTTTTT......TTTTTTTTTT@TTTTTTTTTTTTT...................TTTTTTTTTTTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTTTTTTT..................TTT@@@@@@@@@@@TTTT@@@TTT@@@T.TTT@@@@@@@@@@T@@@@@@@@@@@@@@TTTTTTTTTTTT...TTTTTTTTTT@TTTTTTTTTTTTT....................TTTTTTTTTTTTTTTTTTTT@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTTTTTTTTTTT..................TTT@@@@@@@@@@@@@@TT@@TTTTTTTTTT@@@@@@@@@@TT@@@@@@@@@@@@@@@TTTTTTTTTTTTTTTTTTTTTT@TTTTTTTTTTTTT............................TTTTTTTTTTTTTTTTTT@@@@@@TTT@@@@@@TTTTTTTTTTTTTTTTTTTT..................TTT@@@@@@@@@@@@@@@@TTTTTT@TT@TTT@@@@@@@@@@T@@@@@@@@@@@@@@@@TTTTTTTTTTTTTTTTTTTT@TTTTTTTTTTTTT.............................TTTTTTTTTTTTTTTTTTTTTT@TTTTTT@@TTTTTTTTTTTTTTTTTTTTTT..................TT@@@@@@@@@@@@@@@@@@@TTTTTT@@TTT@@@@@@@@@@TT@@@@@@@@@@@@@@@@@TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT..............................TTTTTTTTT@TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT................TTTT@@@@@@@@@@@@@@@@@@@@@TTTTT@@TT@@@@@@@@@@TT@@@@@@@@@@@@@@@@@@@T@@@@TTTTTTTTTT@TTTTTTTTTTTTT................................TTTTTTTTTTTTT.TTTTTTTTTTTTTTTTTTTTTTT..TTTTTTTTTTTTT.........TTT...TTTTT@@@@@@@@@@@@@@@@@@@@@@TTTTT@@@@@@@@@@@@@T@@@@@@@@@@@@@@@@@@@@TT@TTTTTTTTTT@TTTTTTTTTTTTTT.................................TTTTTTTTTTTT..TTT..TTTTTTTTTTTTTTT.....TTTTTTTTTTTTT........TTTTTTTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTT@@@@@@@@TT@@@@@@@@@@@@@@@@@@@@@T@TTTTTTTT@TTTTTTTT.TTTTT.....................................TTTTTT......TTT......TTTTTTT..........TTTTTTT.TT......TTT.TTTTTT@TTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TT@@@@@TT@@T@@@@@@@@@@@@@@@@@@@@@@TTTTTTTTTTTTTTTTT....TT...................................................TTT......TTTTTT............TTTT...........TTTTTTTTT@TTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TT@@T@@TTTTT@@@@@@@@@@@@@@@@@@@@@@@TTTTTT@TTTTTTT......................................................................TT............................TTTTTTTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTT@TTTT@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTTTTTT...........................................TTT......................................................TTTTTTTTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTT@TTT..........................................TTTTTTT..................................................TTTTTTTTTTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTT@TT..........................................TTTTTTTTT.................................................TTTTTTTTTTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTT@TTT....TTTT..................................TTTTTTTTTT................................................TTTTTTTTTTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TT@@TTT....TTTT.................................TTTTTTTTTTT...............................................TTTTTTTTTTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TT@@TTT....TTTT................................TTTTTTTTTTTT...............................................TTTTTTTTTTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@T@@@TT........................................TTTTTTTTTTTT..............................................TTTTTTTTTTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@T@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@T@@TTT......................................TTTTTTTTTTTTTT............................................TTTTTTTTTTTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@T@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TT@@TT.....................................TTTTTTTTTTTTTTTTTTT.......................TTT...........TTTTTTTTTTTTTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@T@@TTT....................................TTTTTTTTTTTTTTTTTTTT...............TTTTTTTTTTTT.......TTTTTTTTTTTTTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@T@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TT@@TT..................................TTTTTTTTTTTTTTTTTTTTT.....TTT.......TTTTTTTTTTTTTTTT....TTTTTTTTTTTTTTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@T@@TTTT...........................TTTTTTTTTT@TTTT@TTTTTTTTTT....TTTTTTT...TTTTTTTTTTTTTTTTT.....TTTTTTTT....TTT@TT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@T@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TT@@TTT........................TTTTTTTTTTTTTTTTTTTTTTT..TTT.....TTTTTTTTTTTTTTTTTTTTTTTTTTTT....TTTT......TT@TT@TTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@T@@TTTTTTTTTTT............TTTTT@TTTTTTTTTT@TTTTTTTTTT..........TTTTTTTTTTTTTTTTTTTTTTTTTTTT..............TTT@TT@TT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TT@@TTT@TT@TTT..........TTTTTTTTTTTTTTTTT@TTTTTTTTTT...........TTTTTTTTTTTTTTTTTTTTTTTTTTTT............TT@TTTTT@TTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@T@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TT@TTTTTTTTTT.........TTTTTTTTTTTTTTTTTT@TTTTTTTTTT..............TTTTTTTTTTTTTTTTTTTTTT..............TTTTTTT@TT@TT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@T@@TTTTTT@@TT.......TT@TTTTTTTTTTTT@TT@TTTTTTTTTTT.............T....TTTTTTTTTTTTTT.................TTTTTTTTTTTT@TT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@T@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TT@TTTTTT@TTT......TT@@TTTTTTT@@@@@TTT@TT@TTTTTT...............TT..TTTTTTTTTTTTTT..................TTTTTTTTT@TT@TTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@T@@TTTTTTTTT......TT@TTTTTTTTT@@@@TT@TT@@TTTTT.................TT..TTTTTTTTTTTTT...................TTTTTTTTTTTT@TT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@T@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTTTT@TTT...TT@TTTTTTT@TT@@@TT@TTT@TTTTTT....................TTTTTTTTTTT@@@T...................TTTTTTTTTTT@TTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@T@TTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTTTTT@TTTTT@@TTTTTTT@TT@@@TT@TT@@TT@@TT...................TTTTTTTTTTT@@@@@T...................TTTTTTTT@TT@TT@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTTTTTT@@TT@@TTTTTTTT@TT@@TT@TTT@@T@TTTT...................TTTTTTTTTTT@@@@@T....................TTTTTTT@TTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@TTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTT@TTTTTTT@@@@@@TTTTTTTT@TT@TTT@TT@@@TT@TT....................TTTTTTTTTTT@@@@TT......................TTTTTT@TT@TT@@@@@@@@@@@@@@@@@@@@@@@@TT@TTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTTTTTT@@@TTTTTTTTT@TT@TT@TTT@@@T@TTT...................TTTTTTTTTTTTTT@T........................TTTTTTTTTT@TT@@@@@@@@@@@@@@@@@@@@@@TTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTTTTTTT@TTTTTTTTTT@@@TTTTTT@@@TTTTTT.................TTTTTTTTTTTTTT..T..........................TTTTTTTTT@TTT@@@@@@@@@@@@@@@@@@@@@@TTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTTTTTTTTTTTTTTTT@@@TT@TT@@@@TT@TTT...........T....TTTTTTTTTTTTTTT.TT...........................TTTTTTTTT@TT@@@@@@@@@@@@@@@@@@@@@TTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TT@TTTTTTT@@TTTTTTTT@@TT@TTT@@@@TTTTTT........TTTT..TTTTTTTTTTT@@T@TT...............................TTTTTTTT@TTT@@@@@@@@@@@@@@@@@@@TTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTTTT@T@@TTTT@@TT@TT@@@@@TTTTT........TTTTTTTTTTTTTTTTTTTTT@TTTT..............................TTTTTTTT@TT@@@@@@@@@@@@@@@@@@TT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTTTT@@@TTT@@TT@TTTT@@@TTTTTT........TTTTTT@TTTTTTTTTTT@@T@TTTT.T.............................TTTTTTTTTTT@@@@@@@@@@@@@@@TTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTTT@@TT@@TTT@TTTT@@@TTTTTT.........TTTTTTTT@TT@TTTTTTTT@TTTT.TT.............................TTTTTTT@TTT@@@@@@@@@@@@TTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTTTTTT@TT@TTTT@@@TTTTTTT.........TTTTTTTT@TT@TTTT@@TTTTTTTTTTT.............................TTTTTTT@TT@@@@@@@@@@@TT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTTTTTTTTTTTT@T@@TTTTT........TTTTTTTTTTT@@@@@@@@@TTTTTTTTTTT..............................TTT.TT@TTT@@@@@@@@TT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTTTTT@TTTT@@@@TTTTT........TTTTTTTTTTTT@TTT@@@@TTTTTT@TTTT................................T...TT@TTTT@@@@TTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTTTTTTTT@@@@TTTT.........TTTTTTTTTTTTTTTTTTTTTTTTTT@@TT.....................................TT@TTTT@@TTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTTTTT@@@@@TT...........TTTTTTTTTTTTTTTTTTTTTTTTTTTT.....................................T..TTTTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTTTT@@@TTTT............TTTTTTTTTTTT.TTTTTTTTTTTTTT...TTT....TTTTT.....................TTTTTTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTT@@TTTTT.............TTTTTTTTTTT.TTTTTTTTTTTTTT.TTTTT....TTTTT......................T@TTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTT@@TTTTTT............TTTTTTTT....TTTTTTTTTTTTTT.TTTTT....TTTTT.....................TT@@TTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTT@TT@TTT............TTTTTT......TTTTTTTTTTTTTT.TTTTTT...TTTTT.....................TT@@TT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTT@@TTTT.............TT.........TTTTTTTTTTTTTT..TTTTT...TTTT.....................TTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTT........................TTTTTTTTTTTTTTT..TTTTTT..TTTT................TTTTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTT.........................TTTTTTTTTTTTTTT...TTTTT..TTTT..TTTTTTTT......TTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTT........................TTTTTTTTTTTTTTT...TTTTTT.TTTT..TTTTTTTT....TTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTT.....................TTTTTTTTTTTTTTTT....TTTTT.TTTT..TTTTTTTT.TTTTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTT...................TTTTTTTTTTTTTTTT....TTTTT.TTTT..TTTTTTTTTTTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTT..T...............TTTTTTTTTTTTTTTTT....TTTTTTTTT..TTTTTTTTTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTT......TTT......TTTTTTTTTTTTTTTT....TTTTTTTTT........TTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTT..TT.TTTTT.......TTTTTTTTTTTTT.....TTTTTTTTT....TT..TTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTTTTTTT......TTTTTTTTTTTTTT......TTT@TTTT..TTTTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@";
    int which = 0;
    for (int y = 0; y < m->GetMapHeight(); y++)
    {
        for (int x = 0; x < m->GetMapWidth(); x++)
        {
            if (map[which] == '.')
                m->SetTerrainType(x, y, kGround);
            else if (map[which] == 'T')
                m->SetTerrainType(x, y, kTrees);
            else
                m->SetTerrainType(x, y, kOutOfBounds);
            which++;
        }
    }
}

void LoadSimpleMap(Map *m)
{
    m->Scale(4, 4);
    const char map[] = "................";
    int which = 0;
    for (int y = 0; y < m->GetMapHeight(); y++)
    {
        for (int x = 0; x < m->GetMapWidth(); x++)
        {
            if (map[which] == '.')
                m->SetTerrainType(x, y, kGround);
            else if (map[which] == 'T')
                m->SetTerrainType(x, y, kTrees);
            else
                m->SetTerrainType(x, y, kOutOfBounds);
            which++;
        }
    }
}

void LoadMaps(Map *m)
{
    string data;
    ifstream infile;
       infile.open("~/hog2/bin/release/fastmap/lt_gamlenshouse_n.map");
 
       cout << "Reading from the file" << endl;
       infile >> data;

       // write the data at the screen.
       cout << data << endl;
}


double ComputeResidual(Graph *g)
{
    double r=0;
    for(int i=0; i<g->GetNumEdges(); i++){
        r += g->GetEdge(i)->GetWeight();
    }
    return r;
}

void CreateConfidenceInterval(){
    std::string fname = "/home/rezamshy/Outputs/Different_Heuristics/NoE/";
    short nofdcp=10;
    short nofSamples=700;
    short nofp=10;
    clock_t begin = clock();
    DoDH(GraphSearchConstants::kFirstData, nofp);
    GraphMapHeuristicE<graphState> h00(map, g);
    DifferentialHeuristic<graphState> h10(g, GraphSearchConstants::kFirstData, nofp);
    GraphHeuristicContainerE <graphState> h1(g);
    h1.AddHeuristic(&h00);
    h1.AddHeuristic(&h10);
    
    ofstream file;
    file = ofstream(fname+mapName+" - h"+ "1" +" - ci.txt");
    file << "# x y   ylow yhigh\n";
    file.flush();
    
    TemplateAStar<graphState, graphMove, GraphEnvironment> astar;
    std::vector<graphState> p;
    astar.InitializeSearch(ge, 0 , 1, p);
    astar.SetHeuristic(&h1);
    int ne=0;
    std::vector<int> expanded;
    srandom(5333);
    for(int j=0;j<sl->GetNumExperiments();j++){
        /*Experiment e = sl->GetNthExperiment(j);
        xyLoc start, goal;
        start.x = e.GetStartX();
        start.y = e.GetStartY();
        goal.x = e.GetGoalX();
        goal.y = e.GetGoalY();
        astar.GetPath(ge,map->GetNodeNum(start.x, start.y),map->GetNodeNum(goal.x, goal.y),p);
        */
        astar.GetPath(ge,g->GetRandomNode()->GetNum(),g->GetRandomNode()->GetNum(),p);
        int ex = astar.GetNodesExpanded();
        expanded.push_back(ex);
        ne+=ex;
        /*if (abs(ge->GetPathLength(p)-e.GetDistance())>0.01){
            cout<<"Not correct Length"<<endl;
            exit(0);
        }*/
    }
    clock_t end = clock();
    double elapsed_secs = double(end - begin) / CLOCKS_PER_SEC;
    ofstream tFile;
    tFile = ofstream(fname+mapName+" - h"+ "1" +" - t.txt");
    for (short i=0; i<nofdcp;i++){
        tFile<<elapsed_secs<<endl;
        tFile.flush();
    }
    tFile.close();
    cout<<"average nodes expanded by h"<<1<<": "<<ne/sl->GetNumExperiments()<<endl;
    int a [expanded.size()];
    for(int j=0; j < expanded.size(); j++)
        a[j]=expanded[j];
    sort(a, a+expanded.size());
    cout<<"\n Median of ?? in ? is: "<<Median(a,expanded.size())<<endl;
    double *o=ComputeConfidenceInterval(expanded);
    file<< (0+1)*20<<"\t"<<o[0]<<"\t\t"<<o[0]-o[1]<<"\t\t"<<o[0]+o[1]<<endl;
    file.flush();
    for (short i=1; i<nofdcp;i++){
        file<< (i+1)*20<<"\t"<<o[0]<<"\t\t"<<o[0]<<"\t\t"<<o[0]<<endl;
        file.flush();
    }
    file.close();

    
    file = ofstream(fname+mapName+" - h"+ "4" +" - ci.txt");
    file << "# x y   ylow yhigh\n";
    file.flush();
    tFile = ofstream(fname+mapName+" - h"+ "4" +" - t.txt");
    
    
    GraphHeuristicContainerE <graphState> h4(g);
    h4.AddHeuristic(&h00);
    for(int i=0;i<nofdcp;i++){
        
        ne=0;
        expanded.clear();
        short nofcp= (i+1)*20;
        begin = clock();
        DoGDH(GraphSearchConstants::kFirstData+nofp, nofp, nofcp, nofSamples);
        DifferentialHeuristic<graphState> h40(g, GraphSearchConstants::kFirstData+nofp, nofp);
        h4.AddHeuristic(&h40);
        astar.InitializeSearch(ge, 0 , 1, p);
        astar.SetHeuristic(&h4);
        srandom(5333);
        for(int j=0;j<sl->GetNumExperiments();j++){
            /*Experiment e = sl->GetNthExperiment(j);
            xyLoc start, goal;
            start.x = e.GetStartX();
            start.y = e.GetStartY();
            goal.x = e.GetGoalX();
            goal.y = e.GetGoalY();
            astar.GetPath(ge,map->GetNodeNum(start.x, start.y),map->GetNodeNum(goal.x, goal.y),p);
            */
            astar.GetPath(ge,g->GetRandomNode()->GetNum(),g->GetRandomNode()->GetNum(),p);
            int ex = astar.GetNodesExpanded();
            expanded.push_back(ex);
            ne+=ex;
            /*if (abs(ge->GetPathLength(p)-e.GetDistance())>0.01){
                cout<<"Not correct Length"<<endl;
                exit(0);
            }*/
        }
        end = clock();
        elapsed_secs = double(end - begin) / CLOCKS_PER_SEC;
        tFile<<elapsed_secs<<endl;
        tFile.flush();
        cout<<"average nodes expanded by h"<<4<<": "<<ne/sl->GetNumExperiments()<<endl;
        int a2 [expanded.size()];
        for(int j=0; j < expanded.size(); j++)
            a2[j]=expanded[j];
        sort(a2, a2+expanded.size());
        cout<<"\n Median of ?? in ? is: "<<Median(a2,expanded.size())<<endl;
        double *o=ComputeConfidenceInterval(expanded);
        file<< nofcp<<"\t"<<o[0]<<"\t\t"<<o[0]-o[1]<<"\t\t"<<o[0]+o[1]<<endl;
        file.flush();
        h4.RemoveHeuristic();
    }
    tFile.close();
    file.close();
    ge->SetDrawEdgeCosts(false);
    ge->SetColor(Colors::white);
    if (doLerp)
    {
        basege->SetDrawEdgeCosts(false);
        basege->SetColor(Colors::white);
    }
    mapChange = true;
    graphChanged = true;
    /*string myText;
    ifstream readFile(fname+mapName+" - h"+ "4" +".txt");
    std::vector<int> expanded;
    while (getline (readFile, myText)) {
      expanded.push_back(stoi(myText));
    }
    readFile.close();*/
}

double* ComputeConfidenceInterval(std::vector<int> a){
    
    double sum = std::accumulate(a.begin(), a.end(), 0.00);
    double mean = sum / a.size();
    double sq_sum = std::inner_product(a.begin(), a.end(), a.begin(), 0.00);
    double stdev = std::sqrt(sq_sum / (a.size()-1) - mean * mean);
    double confidence=1.96*stdev/std::sqrt(a.size());
    double* o=new double[2];
    o[0]=mean;
    o[1]=confidence;
    
    return o;
}

void FindLargestPart(){
    ResetSeenLabels();
    std::vector<vector<graphState>> ppivots;
    TemplateAStar<graphState, graphMove, GraphEnvironment> astarf;
    std::vector<graphState> p;
    ZeroHeuristic<graphState> z;
    node *n = g->GetRandomNode();
    graphState s, m;
    
    // Finding the parts
    int nofSeen = 0;
    int nofParts = 0;
    while (nofSeen != g->GetNumNodes() ){
        n = g->GetRandomNode();
        if (n->GetLabelF(GraphSearchConstants::kXCoordinate - 1) == 0){
            std::vector<graphState> subPivots;
            
            astarf.SetStopAfterGoal(false);
            astarf.InitializeSearch(ge, n->GetNum() , 0, p);
            astarf.SetHeuristic(&z);
            int counter = 0;
            while (astarf.GetNumOpenItems() > 0)
            {
                s = astarf.GetOpenItem(0).data;
                astarf.DoSingleSearchStep(p);
                g->GetNode(s)->SetLabelF(GraphSearchConstants::kXCoordinate - 1, 1);
                counter++;
            }
            subPivots.push_back(n->GetNum());
            subPivots.push_back(counter);
            ppivots.push_back(subPivots);
    
            nofSeen += counter;
            nofParts++;
        }
    }
    cout<<"\n Number of parts: "<<nofParts<<endl;
    
    // Finding Largest part
    int max = 0;
    int maxIndex = 0;
    for(int i = 0; i < nofParts; i++){
        if(ppivots[i][1] > max){
            maxIndex = i;
            max = ppivots[i][1];
        }
    }
    
    // Marking largest Part
    ResetSeenLabels();
    astarf.SetStopAfterGoal(false);
    astarf.InitializeSearch(ge, ppivots[maxIndex][0] , 0, p);
    astarf.SetHeuristic(&z);
    int counter = 0;
    while (astarf.GetNumOpenItems() > 0)
    {
        s = astarf.GetOpenItem(0).data;
        astarf.DoSingleSearchStep(p);
        g->GetNode(s)->SetLabelF(GraphSearchConstants::kXCoordinate - 1, 1);
        largestPartNodeNumbers.push_back(s);
        counter++;
    }
    //for (int i=0; i<largestPartNodeNumbers.size(); i++) {
    //    cout<<largestPartNodeNumbers[i]<<" ";
    //}
    /*// Balancing the number of pivots in each part
    int counter = 0;
    for(int i = 0; i < nofParts; i++){
        ppivots[i].push_back((ppivots[i][1]*10)/g->GetNumNodes());
        cout<<ppivots[i][2]<<endl;
        std::swap(ppivots[i][0], ppivots[i][2]);
        counter += ppivots[i][0];
    }
    for (int i=0; i < nofp - counter; i++)
        ppivots[i][0]++;
     */
}



int Rank(vector<int> c, int m){
    int rank = 0;
    for (int i=0; i<c.size()-1; i++) {
        rank += c[i];
        rank *= m;
    }
    rank += c[c.size()-1];
    return rank;
}

vector<int> Unrank(int n, int m){
    vector <int> coordinate;
    do{
        coordinate.push_back(n % m);
        n /= m;
    }while(n!=0);
    std::reverse(coordinate.begin(),coordinate.end());
    return coordinate;
}

void CreateGraph(){
    g = new Graph();
    // m*m*m
    int m=50;
    // Adding nodes
    for(int i=0; i<m; i++){
        for(int j=0; j<m; j++){
            for(int k=0; k<m; k++){
                node *n = new node("");
                g->AddNode(n);
            }
        }
    }
    // Adding edges
    //edge *e = new edge(0,99, 1);
    //g->AddEdge(e);
    //srandom(7643);
    vector <int> from, to;
    for(int i=0; i<m; i++){
        for(int j=0; j<m; j++){
            for(int k=0; k<m; k++){
                from.clear();
                to.clear();
                double weight;
                from.push_back(i);from.push_back(j);from.push_back(k);
                if(k<m-1){
                    to.push_back(i);to.push_back(j);to.push_back(k+1);
                    weight = 1 + ((random() / (double)RAND_MAX )) * 1;
                    edge *e = new edge(Rank(from, m),Rank(to, m), weight);
                    g->AddEdge(e);
                }
                if(j<m-1){
                    to.clear();
                    to.push_back(i);to.push_back(j+1);to.push_back(k);
                    weight = 1 + ((random() / (double)RAND_MAX )) * 1;
                    edge *e = new edge(Rank(from, m),Rank(to, m), weight);
                    g->AddEdge(e);
                }
                if(i<m-1){
                    to.clear();
                    to.push_back(i+1);to.push_back(j);to.push_back(k);
                    weight = 1 + ((random() / (double)RAND_MAX )) * 1;
                    edge *e = new edge(Rank(from, m),Rank(to, m), weight);
                    g->AddEdge(e);
                }
            }
        }
    }
    cout<<"3D graph with "<<m<<" nodes in each dimension is created"<<endl;
    cout<<ComputeResidual(g)<<endl;
}

void CreateGraph4D(){
    g = new Graph();
    // m*m*m
    int m=25;
    // Adding nodes
    for(int i=0; i<m; i++){
        for(int j=0; j<m; j++){
            for(int k=0; k<m; k++){
                for (int l = 0; l < m; l++) {
                    node *n = new node("");
                    g->AddNode(n);
                }
            }
        }
    }
    // Adding edges
    //edge *e = new edge(0,99, 1);
    //g->AddEdge(e);
    
    for(int i=0; i<m; i++){
        for(int j=0; j<m; j++){
            for(int k=0; k<m; k++){
                for (int l = 0; l < m; l++) {
                    vector <int> from, to;
                    double weight;
                    from.push_back(i);from.push_back(j);from.push_back(k);from.push_back(l);
                    if(l<m-1){
                        to.push_back(i);to.push_back(j);to.push_back(k);to.push_back(l+1);
                        weight = 1 + ((random() / (double)RAND_MAX )) * 0.5;
                        edge *e = new edge(Rank(from, m),Rank(to, m), weight);
                        g->AddEdge(e);
                    }
                    if(k<m-1){
                        to.clear();
                        to.push_back(i);to.push_back(j);to.push_back(k+1);to.push_back(l);
                        weight = 1 + ((random() / (double)RAND_MAX )) * 0.5;
                        edge *e = new edge(Rank(from, m),Rank(to, m), weight);
                        g->AddEdge(e);
                    }
                    if(j<m-1){
                        to.clear();
                        to.push_back(i);to.push_back(j+1);to.push_back(k);to.push_back(l);
                        weight = 1 + ((random() / (double)RAND_MAX )) * 0.5;
                        edge *e = new edge(Rank(from, m),Rank(to, m), weight);
                        g->AddEdge(e);
                    }
                    if(i<m-1){
                        to.clear();
                        to.push_back(i+1);to.push_back(j);to.push_back(k);to.push_back(l);
                        weight = 1 + ((random() / (double)RAND_MAX )) * 0.5;
                        edge *e = new edge(Rank(from, m),Rank(to, m), weight);
                        g->AddEdge(e);
                    }
                }
            }
        }
    }
    cout<<"4D graph with "<<m<<" nodes in each dimension is created"<<endl;
    cout<<ComputeResidual(g)<<endl;
}

void PrintGraph(Map *m, Graph *g)
{
    
    int gn [m->GetMapHeight()][m->GetMapWidth()];
    int j=-1;
    for (int y = 0; y < m->GetMapHeight(); y++)
    {
        for (int x = 0; x < m->GetMapWidth(); x++)
        {
            if (m->GetTerrainType(x, y)==kGround)
            {
                j++;
                gn[y][x]=j;
            }
            else{
                gn[y][x]=-1;
            }
    //        printf("%d ", gn[y][x]);
        }
    }
    printf("\n");
    
    for (int y = 0; y < m->GetMapHeight()-1; y++)
    {
        for (int x = 0; x < m->GetMapWidth()-1; x++)
        {
            if (gn[y][x]!=-1 && gn[y][x+1]!=-1)
            {
                printf(" *    %.2f    ", g->FindEdge(gn[y][x], gn[y][x+1])->GetWeight());
            }
            else
            {
                printf(" *            ");
            }
        }
        printf(" *\n\n");
        
        for (int x = 0; x < m->GetMapWidth()-1; x++)
        {
            if (gn[y+1][x]!=-1 && gn[y][x+1]!=-1 && gn[y][x]!=-1 && gn[y+1][x+1]!=-1)
            {
                printf("     %.2f     ", g->FindEdge(gn[y][x], gn[y+1][x+1])->GetWeight());
            }
            else
            {
                printf("              ");
            }
        }
        printf("\n");

        for (int x = 0; x < m->GetMapWidth(); x++)
        {
            if (gn[y][x]!=-1 && gn[y+1][x]!=-1)
            {
                printf("%.2f          ", g->FindEdge(gn[y][x], gn[y+1][x])->GetWeight());
            }
            else
            {
                printf("              ");
            }
        }
        printf("\n");

        for (int x = 1; x < m->GetMapWidth(); x++)
        {
            if (gn[y+1][x]!=-1 && gn[y][x-1]!=-1 && gn[y][x]!=-1 && gn[y+1][x-1]!=-1)
            {
                printf("       %.2f   ", g->FindEdge(gn[y][x], gn[y+1][x-1])->GetWeight());
            }
            else
            {
                printf("              ");
            }
        }
        printf("\n\n");
    }
    int y = m->GetMapHeight()-1;
    for (int x = 0; x < m->GetMapWidth()-1; x++)
    {
        if (gn[y][x]!=-1 && gn[y][x+1]!=-1)
        {
            printf(" *    %.2f    ", g->FindEdge(gn[y][x], gn[y][x+1])->GetWeight());
        }
        else
        {
            printf(" *            ");
        }
    }
    printf(" *\n");
}
