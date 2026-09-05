%==========================================================================
% ScriptMonitor.m - Distributed Battery Management System Monitoring
%==========================================================================
% Author: Student Research Project (BMS-EV)
% Hardware: STM32F103C8T6 Master  |  UART 115200 (PA9/PA10)
% CSV: total_mV, soc_tenths, current_mA, temp_tenths, min_mV, max_mV,
%      deltaV_mV, swSetting, swLoad, swBal, cell1..cell15_mV \n
% SW_LabVIEW (PA15) must be ON for telemetry to be transmitted.
%==========================================================================
clc;
clearvars;
delete(findall(0,'Type','figure'));

fprintf('====================================================\n');
fprintf('  Distributed Battery Management System Monitoring   \n');
fprintf('====================================================\n');

MAX_POINTS = 500000;
DEFAULT_BAUD = '115200';
DEFAULT_DUR  = 'inf';

%==========================================================================
% FIGURE 1: Pack Parameters (Total V, Current, Temp, SoC, DeltaV) + Control Panel
%==========================================================================
fig1 = figure('Name','Distributed Battery Management System Monitoring', ...
    'NumberTitle','off','Color','w','Position',[30 30 980 820], ...
    'CloseRequestFcn', @(src,evt) onFigureClose(src));

% --- Control Panel docked at top of fig1 ---
ctrlPanel = uipanel('Parent',fig1,'Title','Control Panel','Units','normalized', ...
    'Position',[0.005 0.880 0.990 0.115],'BackgroundColor','w','FontWeight','bold','FontSize',9);

try
    availPorts = serialportlist("available");
catch
    availPorts = string([]);
end
if isempty(availPorts)
    availPorts = "No ports found";
end

uicontrol(ctrlPanel,'Style','text','String','COM Port:','Units','normalized','Position',[0.010 0.58 0.080 0.32],'BackgroundColor','w','FontWeight','bold','HorizontalAlignment','left');
hPopCOM = uicontrol(ctrlPanel,'Style','popupmenu','String',availPorts,'Units','normalized','Position',[0.090 0.55 0.155 0.40],'BackgroundColor','w');

uicontrol(ctrlPanel,'Style','pushbutton','String','Refresh','Units','normalized','Position',[0.250 0.55 0.080 0.40], ...
    'Callback', @(s,e) refreshPorts(hPopCOM));

uicontrol(ctrlPanel,'Style','text','String','Baudrate:','Units','normalized','Position',[0.345 0.58 0.080 0.32],'BackgroundColor','w','FontWeight','bold','HorizontalAlignment','left');
hEditBaud = uicontrol(ctrlPanel,'Style','edit','String',DEFAULT_BAUD,'Units','normalized','Position',[0.425 0.55 0.110 0.40],'BackgroundColor','w');

uicontrol(ctrlPanel,'Style','text','String','Duration (s) / inf:','Units','normalized','Position',[0.550 0.58 0.150 0.32],'BackgroundColor','w','FontWeight','bold','HorizontalAlignment','left');
hEditDur = uicontrol(ctrlPanel,'Style','edit','String',DEFAULT_DUR,'Units','normalized','Position',[0.700 0.55 0.110 0.40],'BackgroundColor','w','TooltipString','Number of seconds or inf for continuous');

hBtnStart = uicontrol(ctrlPanel,'Style','pushbutton','String','Start','Units','normalized','Position',[0.090 0.07 0.110 0.38], ...
    'BackgroundColor',[0.2 0.7 0.2],'ForegroundColor','w','FontWeight','bold','FontSize',10, ...
    'Callback', @(s,e) onStart());

hBtnStop = uicontrol(ctrlPanel,'Style','pushbutton','String','Stop','Units','normalized','Position',[0.215 0.07 0.110 0.38], ...
    'BackgroundColor',[0.8 0.2 0.2],'ForegroundColor','w','FontWeight','bold','FontSize',10, ...
    'Callback', @(s,e) onStop());

hTxtStatus = uicontrol(ctrlPanel,'Style','text','String','Status: Idle. Select COM, set Baud/Duration, then Start. SW_LabVIEW (PA15) must be ON.', ...
    'Units','normalized','Position',[0.345 0.10 0.400 0.32],'BackgroundColor','w','HorizontalAlignment','left','FontSize',7);

hTxtTimer = uicontrol(ctrlPanel,'Style','text','String','Elapsed: 0.0 s','Units','normalized','Position',[0.830 0.10 0.155 0.35], ...
    'BackgroundColor','w','FontWeight','bold','FontSize',10,'HorizontalAlignment','center');

% --- Pack axes (will be shifted down to make room for panel) ---
ax_total = subplot(3,2,[1 2],'Parent',fig1,'Color','w','XColor','k','YColor','k');
p_total = plot(ax_total, NaN, NaN, 'Color',[0 0.6 0],'LineWidth',2);
ylabel(ax_total,'Total Voltage (mV)','FontWeight','bold');
xlabel(ax_total,'Time (s)','FontWeight','bold');
grid(ax_total,'on'); ax_total.GridColor='k'; ax_total.GridAlpha=0.2;
txt_total = text(ax_total,0.02,0.90,'','Units','normalized','FontWeight','bold','FontSize',8,'BackgroundColor','w','Margin',2);

ax_soc = subplot(3,2,3,'Parent',fig1,'Color','w','XColor','k','YColor','k');
p_soc = plot(ax_soc, NaN, NaN, 'Color',[0 0.4 0.8],'LineWidth',2);
ylabel(ax_soc,'SoC (%)','FontWeight','bold');
xlabel(ax_soc,'Time (s)','FontWeight','bold');
grid(ax_soc,'on'); ax_soc.GridColor='k'; ax_soc.GridAlpha=0.2;
txt_soc = text(ax_soc,0.02,0.90,'','Units','normalized','FontWeight','bold','FontSize',8,'BackgroundColor','w','Margin',2);

ax_curr = subplot(3,2,4,'Parent',fig1,'Color','w','XColor','k','YColor','k');
p_curr = plot(ax_curr, NaN, NaN, 'Color',[0.8 0.6 0],'LineWidth',2);
ylabel(ax_curr,'Current (A)','FontWeight','bold');
xlabel(ax_curr,'Time (s)','FontWeight','bold');
grid(ax_curr,'on'); ax_curr.GridColor='k'; ax_curr.GridAlpha=0.2;
txt_curr = text(ax_curr,0.02,0.90,'','Units','normalized','FontWeight','bold','FontSize',8,'BackgroundColor','w','Margin',2);

ax_temp = subplot(3,2,5,'Parent',fig1,'Color','w','XColor','k','YColor','k');
p_temp = plot(ax_temp, NaN, NaN, 'Color',[0.8 0 0],'LineWidth',2);
ylabel(ax_temp,'Temperature (C)','FontWeight','bold');
xlabel(ax_temp,'Time (s)','FontWeight','bold');
grid(ax_temp,'on'); ax_temp.GridColor='k'; ax_temp.GridAlpha=0.2;
txt_temp = text(ax_temp,0.02,0.90,'','Units','normalized','FontWeight','bold','FontSize',8,'BackgroundColor','w','Margin',2);

ax_delta = subplot(3,2,6,'Parent',fig1,'Color','w','XColor','k','YColor','k');
p_delta = plot(ax_delta, NaN, NaN, 'Color',[0.6 0 0.6],'LineWidth',2);
ylabel(ax_delta,'Delta V (mV)','FontWeight','bold');
xlabel(ax_delta,'Time (s)','FontWeight','bold');
grid(ax_delta,'on'); ax_delta.GridColor='k'; ax_delta.GridAlpha=0.2;
txt_delta = text(ax_delta,0.02,0.90,'','Units','normalized','FontWeight','bold','FontSize',8,'BackgroundColor','w','Margin',2);

linkaxes([ax_total, ax_soc, ax_curr, ax_temp, ax_delta],'x');

% Shift subplot area down to avoid panel overlap (compress to lower ~86%% of figure)
for axH = [ax_total, ax_soc, ax_curr, ax_temp, ax_delta]
    axPos = get(axH,'Position');
    axPos(2) = axPos(2) * 0.86;
    axPos(4) = axPos(4) * 0.86;
    set(axH,'Position',axPos);
end

%==========================================================================
% FIGURE 2: Individual Cell Voltages (10 cells)
%==========================================================================
fig2 = figure('Name','Distributed Battery Management System Monitoring - Cell Voltages', ...
    'NumberTitle','off','Color','w','Position',[1050 200 860 560], ...
    'CloseRequestFcn', @(src,evt) onFigureClose(src));
axCell = axes('Parent',fig2,'Color','w','XColor','k','YColor','k');
hold(axCell,'on');
ylabel(axCell,'Voltage (mV)','FontWeight','bold');
xlabel(axCell,'Time (s)','FontWeight','bold');
grid(axCell,'on'); axCell.GridColor='k'; axCell.GridAlpha=0.2;
colors = lines(10);
p_cells = gobjects(1,10);
for c = 1:10
    p_cells(c) = plot(axCell, NaN, NaN, 'Color', colors(c,:), 'LineWidth',1.5, 'DisplayName', sprintf('Cell %d',c));
end
legend(axCell,'Location','eastoutside');
txt_cells = text(axCell,0.02,0.95,'','Units','normalized','FontWeight','bold','FontSize',8,'BackgroundColor','w','Margin',2);

% Bring fig1 (with docked controls) to front so panel is visible
figure(fig1); drawnow;

% --- Initialize appdata on fig1 (docked controls) ---
setappdata(fig1,'isRunning',false);
setappdata(fig1,'sHandle',[]);
setappdata(fig1,'hPopCOM',hPopCOM);
setappdata(fig1,'hEditBaud',hEditBaud);
setappdata(fig1,'hEditDur',hEditDur);
setappdata(fig1,'hTxtStatus',hTxtStatus);
setappdata(fig1,'hTxtTimer',hTxtTimer);
setappdata(fig1,'fig1',fig1);
setappdata(fig1,'fig2',fig2);
setappdata(fig1,'figCtrl',fig1);
setappdata(fig1,'axHandles',{ax_total, ax_soc, ax_curr, ax_temp, ax_delta, axCell});
setappdata(fig1,'lineHandles',{p_total, p_soc, p_curr, p_temp, p_delta, p_cells});
setappdata(fig1,'txtHandles',{txt_total, txt_soc, txt_curr, txt_temp, txt_delta, txt_cells, hTxtTimer, []});

% Store globally accessible handle for callbacks
assignin('base','gBMSCtrlFig',fig1);
setappdata(0,'BMSCtrlFig',fig1);

fprintf('GUI ready. Select COM port, set Baud/Duration, then click Start.\n');
fprintf('Note: Master SW_LabVIEW switch (PA15) must be ON for telemetry.\n');

%==========================================================================
% Local Functions
%==========================================================================

function refreshPorts(hPop)
    try
        avail = serialportlist("available");
    catch
        avail = string([]);
    end
    if isempty(avail)
        avail = "No ports found";
    end
    set(hPop,'String',avail,'Value',1);
end

function onStart()
    figCtrl = getappdata(0,'BMSCtrlFig');
    if isempty(figCtrl) || ~ishandle(figCtrl), return; end
    if getappdata(figCtrl,'isRunning')
        return;
    end
    hPopCOM  = getappdata(figCtrl,'hPopCOM');
    hEditBaud= getappdata(figCtrl,'hEditBaud');
    hEditDur = getappdata(figCtrl,'hEditDur');
    hTxtStatus=getappdata(figCtrl,'hTxtStatus');

    comList = get(hPopCOM,'String');
    comIdx  = get(hPopCOM,'Value');
    if iscell(comList)
        comStr = string(comList{comIdx});
    elseif isstring(comList)
        comStr = comList(comIdx);
    else
        comStr = string(comList(comIdx,:));
    end
    comStr = strtrim(comStr);
    if comStr=="No ports found" || comStr==""
        set(hTxtStatus,'String','Status: No COM port selected/available.');
        return;
    end
    baudStr = strtrim(get(hEditBaud,'String'));
    baudVal = str2double(baudStr);
    if isnan(baudVal) || baudVal<=0
        set(hTxtStatus,'String','Status: Invalid Baudrate.');
        return;
    end
    durStr = strtrim(get(hEditDur,'String'));
    if strcmpi(durStr,'inf') || strcmpi(durStr,'infinite') || durStr==""
        durationSec = inf;
    else
        durationSec = str2double(durStr);
        if isnan(durationSec) || durationSec<0
            set(hTxtStatus,'String','Status: Invalid Duration. Use number or inf.');
            return;
        end
        if durationSec==0
            durationSec = inf;
        end
    end

    cleanupSerial(comStr);
    s = [];
    try
        s = serialport(comStr, baudVal);
        s.Timeout = 0.2;
        configureTerminator(s,"LF");
        flush(s);
    catch ME
        set(hTxtStatus,'String',sprintf('Status: Failed to open %s: %s',comStr,ME.message));
        return;
    end

    setappdata(figCtrl,'sHandle',s);
    setappdata(figCtrl,'isRunning',true);
    setappdata(figCtrl,'durationSec',durationSec);
    set(hTxtStatus,'String',sprintf('Status: Running on %s @ %d baud | Duration: %s | SW_LabVIEW must be ON',comStr,baudVal,durStr));

    resetPlots(figCtrl);
    runAcquisition(figCtrl);
end

function onStop()
    figCtrl = getappdata(0,'BMSCtrlFig');
    if isempty(figCtrl) || ~ishandle(figCtrl), return; end
    setappdata(figCtrl,'isRunning',false);
    hTxtStatus = getappdata(figCtrl,'hTxtStatus');
    s = getappdata(figCtrl,'sHandle');
    if ~isempty(s)
        try
            pn = s.Port;
            cleanupSerial(pn);
        catch
            cleanupSerial("");
        end
        setappdata(figCtrl,'sHandle',[]);
    end
    if ishandle(hTxtStatus)
        curStr = get(hTxtStatus,'String');
        set(hTxtStatus,'String',sprintf('%s | Stopped.',curStr));
    end
    fprintf('Acquisition stopped by user.\n');
end

function onFigureClose(src)
    figCtrl = getappdata(0,'BMSCtrlFig');
    if ~isempty(figCtrl) && src==figCtrl
        try
            setappdata(figCtrl,'isRunning',false);
            s = getappdata(figCtrl,'sHandle');
            if ~isempty(s)
                try, pn=s.Port; cleanupSerial(pn); catch, cleanupSerial(""); end
            end
        catch
        end
        try, delete(figCtrl); catch, end
        try
            f2 = getappdata(figCtrl,'fig2'); if ishandle(f2), delete(f2); end
        catch
        end
        setappdata(0,'BMSCtrlFig',[]);
        return;
    end
    if ~isempty(figCtrl) && ishandle(figCtrl)
        setappdata(figCtrl,'isRunning',false);
    end
    try, delete(src); catch, end
end

function resetPlots(figCtrl)
    axHandles = getappdata(figCtrl,'axHandles');
    lineHandles = getappdata(figCtrl,'lineHandles');
    txtHandles = getappdata(figCtrl,'txtHandles');
    for k=1:5
        set(lineHandles{1}{k},'XData',NaN,'YData',NaN);
    end
    p_cells = lineHandles{1}{6};
    for c=1:10
        set(p_cells(c),'XData',NaN,'YData',NaN);
    end
    set(txtHandles{1}{1},'String','');
    set(txtHandles{1}{2},'String','');
    set(txtHandles{1}{3},'String','');
    set(txtHandles{1}{4},'String','');
    set(txtHandles{1}{5},'String','');
    set(txtHandles{1}{6},'String','');
    for k=1:6
        if ishandle(axHandles{1}{k})
            xlim(axHandles{1}{k},[0 10]);
            ylim(axHandles{1}{k},'auto');
        end
    end
    drawnow;
end

function runAcquisition(figCtrl)
    hTxtStatus = getappdata(figCtrl,'hTxtStatus');
    hTxtTimer  = getappdata(figCtrl,'hTxtTimer');
    axHandles  = getappdata(figCtrl,'axHandles');
    lineHandles= getappdata(figCtrl,'lineHandles');
    txtHandles = getappdata(figCtrl,'txtHandles');
    fig1 = getappdata(figCtrl,'fig1');
    fig2 = getappdata(figCtrl,'fig2');

    ax_total = axHandles{1}{1}; ax_soc=axHandles{1}{2}; ax_curr=axHandles{1}{3};
    ax_temp=axHandles{1}{4}; ax_delta=axHandles{1}{5}; axCell=axHandles{1}{6};
    p_total=lineHandles{1}{1}; p_soc=lineHandles{1}{2}; p_curr=lineHandles{1}{3};
    p_temp=lineHandles{1}{4}; p_delta=lineHandles{1}{5}; p_cells=lineHandles{1}{6};
    txt_total=txtHandles{1}{1}; txt_soc=txtHandles{1}{2}; txt_curr=txtHandles{1}{3};
    txt_temp=txtHandles{1}{4}; txt_delta=txtHandles{1}{5}; txt_cells=txtHandles{1}{6};

    s = getappdata(figCtrl,'sHandle');
    durationSec = getappdata(figCtrl,'durationSec');

    MAXP = 500000;
    t_buf = zeros(1,MAXP); v_buf=zeros(1,MAXP); soc_buf=zeros(1,MAXP);
    cur_buf=zeros(1,MAXP); tmp_buf=zeros(1,MAXP); del_buf=zeros(1,MAXP);
    c_buf=zeros(10,MAXP);
    num = 0;
    t0 = tic;
    lineBuf = "";

    while true
        if ~ishandle(figCtrl) || ~getappdata(figCtrl,'isRunning')
            break;
        end
        if ~ishandle(fig1) || ~ishandle(fig2)
            setappdata(figCtrl,'isRunning',false);
            break;
        end
        elapsed = toc(t0);
        if isfinite(durationSec) && elapsed >= durationSec
            set(hTxtStatus,'String',sprintf('Status: Duration %.1f s reached. Stopped.',durationSec));
            setappdata(figCtrl,'isRunning',false);
            break;
        end
        set(hTxtTimer,'String',sprintf('Elapsed: %.1f s',elapsed));

        if isempty(s) || ~isvalid(s)
            pause(0.05);
            drawnow limitrate;
            s = getappdata(figCtrl,'sHandle');
            continue;
        end

        try
            nAvail = s.NumBytesAvailable;
        catch
            nAvail = 0;
        end
        if nAvail > 0
            try
                raw = read(s, nAvail, "uint8");
                chunk = char(raw');
                lineBuf = lineBuf + string(chunk);
            catch
                pause(0.005);
                drawnow limitrate;
                continue;
            end
        else
            pause(0.003);
            drawnow limitrate;
            if num==0 && elapsed>2
                set(txt_total,'String','Waiting for data... Check SW_LabVIEW (PA15) ON');
            end
            continue;
        end

        lines = split(lineBuf, newline);
        if endsWith(lineBuf, newline)
            complete = lines(1:end-1);
            lineBuf = "";
        else
            complete = lines(1:end-1);
            lineBuf = lines(end);
        end
        if isempty(complete)
            drawnow limitrate;
            continue;
        end

        didUpdate = false;
        for li = 1:numel(complete)
            ln = strtrim(complete(li));
            if strlength(ln)==0, continue; end
            fields = strsplit(ln, ',');
            if numel(fields) < 20, continue; end
            nums = str2double(fields);
            if any(isnan(nums(1:20))), continue; end
            num = num + 1;
            if num > MAXP
                keep = floor(MAXP*0.6);
                t_buf(1:keep) = t_buf(end-keep+1:end); v_buf(1:keep)=v_buf(end-keep+1:end);
                soc_buf(1:keep)=soc_buf(end-keep+1:end); cur_buf(1:keep)=cur_buf(end-keep+1:end);
                tmp_buf(1:keep)=tmp_buf(end-keep+1:end); del_buf(1:keep)=del_buf(end-keep+1:end);
                c_buf(:,1:keep)=c_buf(:,end-keep+1:end);
                num = keep+1;
            end
            t_buf(num) = toc(t0);
            v_buf(num) = nums(1);
            soc_buf(num) = nums(2)/10;
            cur_buf(num) = nums(3)/1000;
            tmp_buf(num) = nums(4)/10;
            del_buf(num) = nums(7);
            c_buf(:,num) = nums(11:20);
            didUpdate = true;
        end
        if ~didUpdate
            drawnow limitrate;
            continue;
        end

        xData = t_buf(1:num);
        set(p_total,'XData',xData,'YData',v_buf(1:num));
        set(p_soc,'XData',xData,'YData',soc_buf(1:num));
        set(p_curr,'XData',xData,'YData',cur_buf(1:num));
        set(p_temp,'XData',xData,'YData',tmp_buf(1:num));
        set(p_delta,'XData',xData,'YData',del_buf(1:num));
        for c=1:10
            set(p_cells(c),'XData',xData,'YData',c_buf(c,1:num));
        end

        tNow = xData(end);
        xMax = max(1, tNow*1.02);
        xMin = 0;
        xlim(ax_delta,[xMin xMax]);
        xlim(axCell,[xMin xMax]);
        ylim(ax_total,'auto'); ylim(ax_soc,'auto'); ylim(ax_curr,'auto');
        ylim(ax_temp,'auto'); ylim(ax_delta,'auto'); ylim(axCell,'auto');

        set(txt_total,'String',sprintf('%.0f mV | %.1f s',v_buf(num),tNow));
        set(txt_soc,'String',sprintf('%.1f %% | %.1f s',soc_buf(num),tNow));
        set(txt_curr,'String',sprintf('%.3f A | %.1f s',cur_buf(num),tNow));
        set(txt_temp,'String',sprintf('%.1f C | %.1f s',tmp_buf(num),tNow));
        set(txt_delta,'String',sprintf('%.0f mV | %.1f s',del_buf(num),tNow));
        set(txt_cells,'String',sprintf('Min %.0f mV | Max %.0f mV | %.1f s',min(c_buf(:,num)),max(c_buf(:,num)),tNow));

        drawnow limitrate;
    end

    s2 = getappdata(figCtrl,'sHandle');
    if ~isempty(s2)
        try
            pn = s2.Port; cleanupSerial(pn);
        catch
            cleanupSerial("");
        end
        setappdata(figCtrl,'sHandle',[]);
    end
    setappdata(figCtrl,'isRunning',false);
    if ishandle(hTxtTimer)
        set(hTxtTimer,'String',sprintf('Elapsed: %.1f s | Stopped',toc(t0)));
    end
end

function cleanupSerial(portName)
    try
        if strlength(portName)>0
            old = serialportfind("Port",portName);
        else
            old = serialportfind;
        end
        if ~isempty(old)
            delete(old);
        end
    catch
    end
end
