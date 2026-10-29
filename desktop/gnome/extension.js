// SPDX-License-Identifier: AGPL-3.0-or-later
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import St from 'gi://St';
import Clutter from 'gi://Clutter';
import Pango from 'gi://Pango';
import Atk from 'gi://Atk';
import Shell from 'gi://Shell';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import * as PanelMenu from 'resource:///org/gnome/shell/ui/panelMenu.js';
import {Extension, gettext as _, ngettext} from 'resource:///org/gnome/shell/extensions/extension.js';
import {PaletteChoice} from './contrast.mjs';
import {Tracker, sections} from './tracker.mjs';
import {LocalEngine} from './local-engine.js';
import {tokens as T} from './tokens.js';

const C = {...T.color};
const N = T.native;
// Translated when used: translations are only available once the extension is loaded.
const sample = () => _('A little preparation makes room for a better conversation. [smile]\n\nTake your time. Leave space for a pause. Your next words will be here when you are ready.');
const SETUP_URL = 'https://github.com/brianidavide/kalipso/blob/main/docs/local-engine.md';
const colorNote = () => _('Automatic samples colors around the reader. No images are saved.');
const ROLES = {overlay: 'background', overlayText: 'text', overlayMuted: 'muted', overlaySurface: 'surface',
    overlayLine: 'hover', overlayBorder: 'border', focus: 'focus', reading: 'accent'};

// Script, reading position and preferences, saved as JSON in the user data folder.
class SavedState {
    constructor() {
        this._file = Gio.File.new_for_path(GLib.build_filenamev([GLib.get_user_data_dir(), 'kalipso', 'gnome-extension.json']));
        this._timer = 0;
        this._loaded = false;
        this.values = {};
    }

    load(callback) {
        this._file.load_contents_async(null, (file, result) => {
            try {
                const [, bytes] = file.load_contents_finish(result);
                this.values = JSON.parse(new TextDecoder().decode(bytes)) ?? {};
            } catch {
                this.values = {}; // First run, or an unreadable file: start fresh.
            }
            this._loaded = true;
            callback({...this.values});
        });
    }

    // Coalesces frequent updates (typing, reading) into one write per second.
    update(changes) {
        // Nothing is written before the saved state has been read.
        if (!this._loaded || Object.entries(changes).every(([key, value]) => this.values[key] === value))
            return;
        Object.assign(this.values, changes);
        if (!this._timer)
            this._timer = GLib.timeout_add_seconds(GLib.PRIORITY_LOW, 1, () => {
                this._timer = 0;
                this.write();
                return GLib.SOURCE_REMOVE;
            });
    }

    write() {
        try {
            this._file.get_parent().make_directory_with_parents(null);
        } catch (error) {
            if (!error.matches(Gio.IOErrorEnum, Gio.IOErrorEnum.EXISTS))
                return console.error(`Kalipso: cannot create ${this._file.get_parent().get_path()}: ${error.message}`);
        }
        const bytes = new GLib.Bytes(new TextEncoder().encode(JSON.stringify(this.values)));
        this._file.replace_contents_bytes_async(bytes, null, false, Gio.FileCreateFlags.REPLACE_DESTINATION, null,
            (file, result) => {
                try {
                    file.replace_contents_finish(result);
                } catch (error) {
                    console.error(`Kalipso: cannot save the script: ${error.message}`);
                }
            });
    }

    flush() {
        if (!this._timer)
            return;
        GLib.source_remove(this._timer);
        this._timer = 0;
        this.write();
    }
}

export default class Kalipso extends Extension {
    enable() {
        Object.assign(C, T.color);
        this._appearance = 'dark';
        this._resolvedAppearance = 'dark';
        this._paletteChoice = new PaletteChoice();
        this._colorTimer = 0;
        this._colorBusy = false;
        this._colorPoint = 0;
        this._colorSamples = 0;
        this._originalStyles = new WeakMap();
        this._buttons = [];
        this._rows = [];
        this._steadyTimer = 0;
        this._mode = 'manual';
        this._speed = 140;
        this._mirror = 'none';
        this._readerSize = N.readerSize;
        this._fontFamily = '';     // '' is the interface font
        this._lineSpacing = 100;   // Percent of the normal line height
        this._countdown = 3;       // Seconds before steady scrolling starts; 0 is off
        this._countdownLeft = 0;
        this._countdownTimer = 0;
        this._pane = '';
        this._status = _('Choose a speech engine to check its setup.');
        this._speechState = 'idle';
        this._lastSequence = -1;
        this._backend = 'moonshine';
        this._active = false;
        this.diagnostics = GLib.getenv('KALIPSO_TEST_API') === '1';
        this.tracker = new Tracker(sample());
        this.state = new SavedState();

        this.local = new LocalEngine(state => {
            this._status = state.message || state.status;
            this._speechState = state.status;
            this._active = Boolean(state.active);
            if (state.backend)
                this._backend = state.backend;
            for (const event of state.events ?? [state]) {
                if (Number.isInteger(event.sequence) && event.sequence > this._lastSequence) {
                    this.tracker.follow(event.text, event.final);
                    this._lastSequence = event.sequence;
                }
            }
            this.render();
        });

        this._buildIndicator();
        this._buildReader();
        this.desktopSettings = new Gio.Settings({schema_id: 'org.gnome.desktop.interface'});
        this.monitorSignal = Main.layoutManager.connect('monitors-changed', () => this.place());
        this.fontSignal = this.desktopSettings.connect('changed::text-scaling-factor', () => this.render());
        if (this.diagnostics)
            this._exportTestApi();
        this.appearance('dark');
        this.state.load(values => this._restore(values));
    }

    disable() {
        this.stopSteady();
        this.stopColorSampling();
        this.state?.flush();
        this.state = null;
        this.local?.destroy();
        this.local = null;
        this.testApi?.unexport();
        this.testApi = null;
        if (this.monitorSignal)
            Main.layoutManager.disconnect(this.monitorSignal);
        this.monitorSignal = 0;
        if (this.fontSignal)
            this.desktopSettings.disconnect(this.fontSignal);
        this.fontSignal = 0;
        this.desktopSettings = null;
        this.actor?.destroy();
        this.actor = null;
        this.indicator?.destroy();
        this.indicator = null;
    }

    _restore(values) {
        if (!this.actor)
            return;
        if (typeof values.script === 'string')
            this.editor.set_text(values.script); // Updates the tracker through text-changed.
        if (Number.isInteger(values.position) && values.position >= 0)
            this.tracker.seekCharacter(values.position);
        if (['manual', 'steady', 'voice'].includes(values.mode))
            this._mode = values.mode;
        if (Number.isInteger(values.speed))
            this._speed = Math.max(40, Math.min(400, values.speed));
        if (['none', 'horizontal', 'vertical'].includes(values.mirror))
            this.setMirror(values.mirror);
        if (typeof values.fontFamily === 'string')
            this._fontFamily = values.fontFamily;
        if ([100, 125, 150].includes(values.lineSpacing))
            this._lineSpacing = values.lineSpacing;
        if ([0, 3, 5].includes(values.countdown))
            this._countdown = values.countdown;
        if (Number.isInteger(values.readerSize))
            this.resize(values.readerSize - this._readerSize);
        else
            this.resize(0);
        if (['auto', 'dark', 'light'].includes(values.appearance))
            this.appearance(values.appearance);
        this.render();
    }

    _buildIndicator() {
        this.indicator = new PanelMenu.Button(0, 'Kalipso', true);
        this.indicator.add_child(new St.Icon({icon_name: 'media-playback-start-symbolic', style_class: 'system-status-icon'}));
        this.indicator.connect('button-press-event', () => {
            this.toggle();
            return Clutter.EVENT_STOP;
        });
        this.indicator.connect('key-press-event', (_actor, event) => {
            if ([Clutter.KEY_Return, Clutter.KEY_space].includes(event.get_key_symbol())) {
                this.toggle();
                return Clutter.EVENT_STOP;
            }
            return Clutter.EVENT_PROPAGATE;
        });
        Main.panel.addToStatusArea(this.uuid, this.indicator);
    }

    _row() {
        const box = new St.BoxLayout({style: `spacing:${T.space[2]};`, x_expand: true});
        this._rows.push(box);
        return box;
    }

    _group(text) {
        return new St.Label({text, style_class: 'tp-group-label'});
    }

    _note(text) {
        const label = new St.Label({text, style: `font-size:${N.labelSize}pt;color:${C.overlayMuted};`});
        label.clutter_text.set_line_wrap(true);
        label.clutter_text.set_ellipsize(Pango.EllipsizeMode.NONE);
        return label;
    }

    _buildReader() {
        this.actor = new St.BoxLayout({
            orientation: Clutter.Orientation.VERTICAL, reactive: true, can_focus: false, style_class: 'tp-dark',
            style: `background-color:${C.overlay};color:${C.overlayText};padding:${N.padding}px;spacing:${N.gap}px;` +
                `border:2px solid ${C.overlayBorder};border-radius:${N.radius}px;`,
        });

        const header = this._row();
        this.header = header;
        this.title = new St.Label({text: 'Kalipso', x_expand: true, y_align: Clutter.ActorAlign.CENTER,
            style_class: 'tp-title', style: `font-size:${N.labelSize}pt;`});
        this.title.clutter_text.set_ellipsize(Pango.EllipsizeMode.END);
        header.add_child(this.title);
        this.previousSection = this.button(_('‹ Section'), () => this.jumpSection(-1), 'quiet');
        this.previousSection.accessible_name = _('Previous section');
        this.nextSection = this.button(_('Section ›'), () => this.jumpSection(1), 'quiet');
        this.nextSection.accessible_name = _('Next section');
        this.editButton = this.button(_('Edit script'), () => this.showPane(this._pane === 'edit' ? '' : 'edit'), 'quiet');
        this.settingsButton = this.button(_('Settings'), () => this.showPane(this._pane === 'settings' ? '' : 'settings'), 'quiet');
        for (const child of [this.previousSection, this.nextSection, this.editButton, this.settingsButton,
            this.button(_('Hide'), () => this.hide(), 'quiet')])
            header.add_child(child);

        this.progress = new St.Widget({style_class: 'tp-progress', x_expand: true, layout_manager: new Clutter.BinLayout()});
        this.progressFill = new St.Widget({style_class: 'tp-progress-fill', x_expand: false, x_align: Clutter.ActorAlign.START});
        this.progress.add_child(this.progressFill);
        this.reader = new Clutter.Text({font_name: `Sans ${N.readerSize}`, line_wrap: true, ellipsize: 0,
            y_align: Clutter.ActorAlign.START, x_align: Clutter.ActorAlign.START});
        this.reader.set_pivot_point(0.5, 0.5);
        this.readerWrap = new St.Widget({layout_manager: new Clutter.BinLayout(), x_expand: true});
        this.readerWrap.add_child(this.reader);
        // Seconds left before steady scrolling starts, over the text.
        this.countdownLabel = new St.Label({style_class: 'tp-countdown', visible: false,
            x_align: Clutter.ActorAlign.CENTER, y_align: Clutter.ActorAlign.CENTER});
        this.readerWrap.add_child(this.countdownLabel);
        this.readerWrap.clip_to_allocation = true;

        this.transport = this._row();
        this.primary = this.button(_('Next words'), () => this.primaryAction(), 'primary');
        this.previous = this.button(_('Previous'), () => this.seek(this.tracker.position - 4), 'quiet');
        this.reset = this.button(_('Reset'), () => this.seek(-1), 'quiet');
        for (const child of [this.primary, this.previous, this.reset])
            this.transport.add_child(child);
        this.counter = new St.Label({x_expand: true, x_align: Clutter.ActorAlign.END, y_align: Clutter.ActorAlign.CENTER,
            style: `font-size:${N.labelSize}pt;color:${C.overlayMuted};`});
        this.transport.add_child(this.counter);

        this.speedRow = this._row();
        this.speedLabel = new St.Label({y_align: Clutter.ActorAlign.CENTER});
        this.speedRow.add_child(this.button(_('Slower'), () => this.changeSpeed(-10), 'quiet'));
        this.speedRow.add_child(this.speedLabel);
        this.speedRow.add_child(this.button(_('Faster'), () => this.changeSpeed(10), 'quiet'));

        this.stateLabel = new St.Label({accessible_role: Atk.Role.STATUSBAR, style: `font-size:${N.labelSize}pt;color:${C.reading};`});

        this._buildEditPane();
        this._buildSettingsPane();
        this.secondaryContent = new St.BoxLayout({orientation: Clutter.Orientation.VERTICAL, x_expand: true});
        this.secondaryContent.add_child(this.editPane);
        this.secondaryContent.add_child(this.settingsPane);
        this.secondaryScroll = new St.ScrollView({x_expand: true});
        this.secondaryScroll.set_child(this.secondaryContent);

        this.mainContent = new St.BoxLayout({orientation: Clutter.Orientation.VERTICAL, x_expand: true, style: `spacing:${N.gap}px;`});
        for (const child of [header, this.progress, this.readerWrap, this.transport, this.speedRow, this.stateLabel, this.secondaryScroll])
            this.mainContent.add_child(child);
        this.mainScroll = new St.ScrollView({x_expand: true, hscrollbar_policy: St.PolicyType.NEVER});
        this.mainScroll.set_child(this.mainContent);
        this.actor.add_child(this.mainScroll);
        this.actor.connect('key-press-event', (_actor, event) => this._onKey(event));
        Main.layoutManager.addChrome(this.actor, {trackFullscreen: true});
    }

    _buildEditPane() {
        this.editPane = new St.BoxLayout({orientation: Clutter.Orientation.VERTICAL,
            style: `spacing:${N.gap}px;padding-top:${N.gap}px;border-top:1px solid ${C.overlayLine};`});
        this.editPane.add_child(new St.Label({text: _('SCRIPT'), style: `font-size:${N.labelSize}pt;color:${C.overlayMuted};`}));
        this.editor = new St.Entry({accessible_name: _('Script'), text: sample(), can_focus: true, x_expand: true,
            style: `background-color:${C.surface};color:${C.ink};border-radius:${N.buttonRadius}px;padding:${N.gap}px;` +
                `font-size:${N.bodySize}pt;min-height:${N.padding * 4}px;`});
        this.editor.clutter_text.set_single_line_mode(false);
        this.editor.clutter_text.set_line_wrap(true);
        this.editor.clutter_text.connect('key-focus-in', () => this.reveal(this.editor));
        this.editor.clutter_text.connect('text-changed', () => {
            const text = this.editor.get_text();
            if (text === this.tracker.text)
                return;
            this.tracker.setText(text);
            this.state?.update({script: text});
            this.render();
        });
        const editScroll = new St.ScrollView({style: `max-height:${N.editorHeight + N.padding * 2}px;`, x_expand: true});
        const editContent = new St.BoxLayout({orientation: Clutter.Orientation.VERTICAL, x_expand: true});
        editContent.add_child(this.editor);
        editScroll.set_child(editContent);
        this.editPane.add_child(editScroll);
        this.editPane.add_child(this._note(_('Saved automatically. [Bracketed cues] are shown but not read aloud. Lines starting with # are sections.')));
    }

    _buildSettingsPane() {
        this.settingsPane = new St.BoxLayout({orientation: Clutter.Orientation.VERTICAL,
            style: `spacing:${N.gap}px;padding-top:${N.gap}px;border-top:1px solid ${C.overlayLine};`});
        this.settingsPane.add_child(this._group(_('Reading mode')));
        const modeRow = this._row();
        this.modeButtons = [['manual', _('Manual')], ['steady', _('Steady speed')], ['voice', _('Follow my voice')]]
            .map(([id, label]) => [id, label, this.button(label, () => this.mode(id))]);
        for (const [, , button] of this.modeButtons)
            modeRow.add_child(button);
        this.settingsPane.add_child(modeRow);

        this.settingsPane.add_child(this._group(_('Speech engine')));
        const engineRow = this._row();
        this.engineButtons = [];
        for (const id of ['moonshine', 'vosk']) { // Product names, not translated.
            const button = this.button(id === 'moonshine' ? 'Moonshine' : 'Vosk', () => this.chooseEngine(id));
            this.engineButtons.push([id, button]);
            engineRow.add_child(button);
        }
        this.settingsPane.add_child(engineRow);
        this.details = this._note('');
        this.settingsPane.add_child(this.details);

        this.settingsPane.add_child(this._group(_('Reading size')));
        const sizeRow = this._row();
        sizeRow.add_child(this.button(_('Smaller'), () => this.resize(-2)));
        sizeRow.add_child(this.button(_('Larger'), () => this.resize(2)));
        this.settingsPane.add_child(sizeRow);

        this.settingsPane.add_child(this._group(_('Reader font')));
        const fonts = this._row();
        // A generic serif, plus fonts designed for easier reading when installed.
        const installed = new Set(this.reader.get_pango_context().list_families().map(family => family.get_name()));
        this.fontButtons = [['', _('Interface font')], ['Serif', _('Serif')],
            ...['Atkinson Hyperlegible', 'Atkinson Hyperlegible Next', 'OpenDyslexic', 'Lexend']
                .filter(family => installed.has(family)).map(family => [family, family])]
            .map(([id, label]) => [id, label, this.button(label, () => this.setFont(id))]);
        for (const [, , button] of this.fontButtons)
            fonts.add_child(button);
        this.settingsPane.add_child(fonts);

        this.settingsPane.add_child(this._group(_('Line spacing')));
        const spacing = this._row();
        this.spacingButtons = [[100, _('Normal')], [125, _('Relaxed')], [150, _('Wide')]]
            .map(([id, label]) => [id, label, this.button(label, () => this.setLineSpacing(id))]);
        for (const [, , button] of this.spacingButtons)
            spacing.add_child(button);
        this.settingsPane.add_child(spacing);

        this.settingsPane.add_child(this._group(_('Countdown before steady scrolling')));
        const countdown = this._row();
        this.countdownButtons = [0, 3, 5]
            .map(id => [id, id ? ngettext('%1 second', '%1 seconds', id).replace('%1', id) : _('Off')])
            .map(([id, label]) => [id, label, this.button(label, () => {
                this._countdown = id;
                this.state?.update({countdown: id});
                this.render();
            })]);
        for (const [, , button] of this.countdownButtons)
            countdown.add_child(button);
        this.settingsPane.add_child(countdown);

        this.settingsPane.add_child(this._group(_('Reader colors')));
        const colors = this._row();
        this.appearanceButtons = [['auto', _('Automatic')], ['dark', _('Dark')], ['light', _('Light')]]
            .map(([id, label]) => [id, label, this.button(label, () => this.appearance(id))]);
        for (const [, , button] of this.appearanceButtons)
            colors.add_child(button);
        this.settingsPane.add_child(colors);
        this.colorDetails = this._note(colorNote());
        this.settingsPane.add_child(this.colorDetails);

        this.settingsPane.add_child(this._group(_('Mirror')));
        const mirror = this._row();
        this.mirrorButtons = [['none', _('Off')], ['horizontal', _('Horizontal')], ['vertical', _('Vertical')]]
            .map(([id, label]) => [id, label, this.button(label, () => this.setMirror(id))]);
        for (const [, , button] of this.mirrorButtons)
            mirror.add_child(button);
        this.settingsPane.add_child(mirror);

        this.settingsPane.add_child(this.button(_('Local speech setup ↗'), () => {
            try {
                Gio.AppInfo.launch_default_for_uri(SETUP_URL, null);
            } catch (error) {
                this._status = String(error.message);
                this.render();
            }
        }));
        this.privacy = this._note(_('Keys: arrows or Page Up/Down step, Space starts or pauses, + and − change the steady speed. The reader may appear in screen sharing; check what other participants see.'));
        this.settingsPane.add_child(this.privacy);
    }

    _exportTestApi() {
        const xml = '<node><interface name="io.github.brianidavide.Kalipso.Reader">' +
            '<method name="GetState"><arg type="s" direction="out"/></method>' +
            '<method name="SetScript"><arg type="s" direction="in"/></method>' +
            '<method name="SetAppearance"><arg type="s" direction="in"/></method>' +
            '<method name="Start"/><method name="Stop"/></interface></node>';
        const controls = () => [this.primary, this.previous, this.reset, this.editButton, this.settingsButton,
            ...this.engineButtons.map(item => item[1]), ...this.modeButtons.map(item => item[2])];
        this.testApi = Gio.DBusExportedObject.wrapJSObject(xml, {
            GetState: () => JSON.stringify({
                appearance: this._appearance, resolvedAppearance: this._resolvedAppearance,
                colorStatus: this.colorDetails.text, colorSamples: this._colorSamples, position: this.tracker.position,
                script: this.tracker.text, status: this._status, mode: this._mode, pane: this._pane,
                mirror: this._mirror, visible: this.actor.visible,
                controls: controls().map(button => {
                    const [x, y] = button.get_transformed_position();
                    return {label: button.label, x, y, width: button.width, height: button.height, focused: button.has_key_focus()};
                }),
                markup: this.tracker.markup(), attributes: this.reader.get_attributes()?.to_string(),
            }),
            SetScript: text => this.editor.set_text(text),
            SetAppearance: theme => { if (['auto', 'light', 'dark'].includes(theme)) this.appearance(theme); },
            Start: () => { this._mode = 'voice'; this.start(); },
            Stop: () => this.stop(),
        });
        this.testApi.export(Gio.DBus.session, '/io/github/brianidavide/Kalipso/Reader');
    }

    // kind: '' (outlined), 'primary' (the one main action) or 'quiet' (window actions).
    button(label, action, kind = '') {
        const button = new St.Button({label, can_focus: true, reactive: true,
            style_class: kind ? `tp-button tp-${kind}` : 'tp-button'});
        const text = button.child.clutter_text ?? button.child;
        text.set_line_wrap(true);
        text.set_ellipsize(Pango.EllipsizeMode.NONE);
        this._buttons.push(button);
        button.connect('key-focus-in', () => this.reveal(button));
        button.connect('clicked', () => {
            button.grab_key_focus();
            action();
        });
        return button;
    }

    _onKey(event) {
        const key = event.get_key_symbol();
        if (key === Clutter.KEY_Escape) {
            if (this._pane)
                this.showPane('');
            else
                this.hide();
            return Clutter.EVENT_STOP;
        }
        if (key === Clutter.KEY_Tab || key === Clutter.KEY_ISO_Left_Tab) {
            const backward = key === Clutter.KEY_ISO_Left_Tab || (event.get_state() & Clutter.ModifierType.SHIFT_MASK);
            if (this.actor.navigate_focus(global.stage.get_key_focus(), backward ? St.DirectionType.TAB_BACKWARD
                : St.DirectionType.TAB_FORWARD, false))
                return Clutter.EVENT_STOP;
            return Clutter.EVENT_PROPAGATE;
        }
        if (this._pane === 'edit')
            return Clutter.EVENT_PROPAGATE;
        // Foot pedals and presentation clickers send these keys.
        const actions = new Map([
            [Clutter.KEY_Right, () => this.seek(this.tracker.position + 1)],
            [Clutter.KEY_Down, () => this.seek(this.tracker.position + 1)],
            [Clutter.KEY_Page_Down, () => this.seek(this.tracker.position + 1)],
            [Clutter.KEY_Left, () => this.seek(this.tracker.position - 1)],
            [Clutter.KEY_Up, () => this.seek(this.tracker.position - 1)],
            [Clutter.KEY_Page_Up, () => this.seek(this.tracker.position - 1)],
            [Clutter.KEY_Home, () => this.seek(-1)],
            [Clutter.KEY_plus, () => this.changeSpeed(10)],
            [Clutter.KEY_equal, () => this.changeSpeed(10)],
            [Clutter.KEY_minus, () => this.changeSpeed(-10)],
        ]);
        const action = actions.get(key);
        if (!action)
            return Clutter.EVENT_PROPAGATE;
        action();
        return Clutter.EVENT_STOP;
    }

    reveal(actor) {
        for (let parent = actor.get_parent(); parent; parent = parent.get_parent()) {
            if (!(parent instanceof St.ScrollView))
                continue;
            const adjustment = parent.get_vadjustment(), top = parent.get_transformed_position()[1];
            const y = actor.get_transformed_position()[1];
            const delta = y < top ? y - top : y + actor.height > top + parent.height ? y + actor.height - top - parent.height : 0;
            adjustment.value = Math.max(adjustment.lower, Math.min(adjustment.upper - adjustment.page_size, adjustment.value + delta));
        }
    }

    place() {
        const monitor = Main.layoutManager.primaryMonitor;
        if (!monitor || !this.actor)
            return;
        const scale = this.desktopSettings.get_double('text-scaling-factor');
        this.header.get_first_child().visible = scale < 1.6;
        const width = Math.min(N.width * Math.min(scale, 1.2), monitor.width - 32);
        const innerWidth = width - N.padding * 2 - 32;
        this.actor.width = width;
        this.mainContent.width = innerWidth;
        for (const button of this._buttons)
            button.height = Math.ceil(N.labelSize * 4 / 3 * scale * 1.6 + 20);
        // Stack a row's controls when they do not fit side by side.
        const stack = (row, vertical) => {
            row.orientation = vertical ? Clutter.Orientation.VERTICAL : Clutter.Orientation.HORIZONTAL;
        };
        for (const row of this._rows) {
            stack(row, false);
            stack(row, row.get_preferred_width(-1)[1] > innerWidth - 24);
        }
        stack(this.header, width < 500 * scale);
        stack(this.transport, width < 500 * scale);
        this.mainScroll.set_style(`max-height:${Math.max(120, monitor.height - Main.panel.height - 48 - N.padding * 2)}px;`);
        this.primary.width = Math.min(width - N.padding * 2, Math.ceil((N.primaryWidth - 28) * scale + 28));
        this.reader.width = innerWidth;
        for (const actor of [this.details, this.privacy, this.secondaryContent, this.editor])
            actor.width = innerWidth - 20;
        this.actor.set_position(monitor.x + Math.round((monitor.width - width) / 2), monitor.y + Main.panel.height + 16);

        // Show whole lines of the reading excerpt in the space left by the controls.
        const controls = N.padding * 2 + N.gap * 7 + 3 + this.header.get_preferred_height(width)[1] +
            this.transport.get_preferred_height(width)[1] + this.stateLabel.get_preferred_height(width)[1] +
            (this.speedRow.visible ? this.speedRow.get_preferred_height(width)[1] : 0);
        const available = monitor.height - Main.panel.height - 32 - controls - (this._pane ? 120 : 0);
        // Distance between lines, including any extra line height.
        const layout = this.reader.get_layout();
        const lineHeight = Math.max(1, Math.round(layout.get_pixel_extents()[1].height / Math.max(1, layout.get_line_count())));
        const height = Math.min(Math.round(this._readerSize * scale * 4 / 3 * 4.5), Math.max(60, available));
        this.readerWrap.height = Math.max(lineHeight, Math.floor(height / lineHeight) * lineHeight);
        this.secondaryScroll.set_style(`max-height:${Math.max(80, monitor.height - Main.panel.height - 32 - controls - this.readerWrap.height)}px;`);
    }

    appearance(theme) {
        this._appearance = theme;
        this.stopColorSampling();
        this.colorDetails?.set_text(colorNote());
        this.state?.update({appearance: theme});
        if (theme === 'auto') {
            this.sampleColors();
            this._colorTimer = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 2000, () => {
                this.sampleColors();
                return GLib.SOURCE_CONTINUE;
            });
        } else {
            this.applyAppearance(theme);
        }
    }

    stopColorSampling() {
        if (this._colorTimer)
            GLib.source_remove(this._colorTimer);
        this._colorTimer = 0;
    }

    sampleColors() {
        if (!this.actor?.visible || this._appearance !== 'auto' || this._colorBusy)
            return;
        const [x, y] = this.actor.get_transformed_position(), width = this.actor.width, height = this.actor.height;
        const points = [[x - 3, y + height / 2], [x + width + 3, y + height / 2], [x + width / 2, y - 3], [x + width / 2, y + height + 3]];
        const point = points[this._colorPoint++ % points.length].map(Math.round);
        if (point[0] < 0 || point[1] < 0 || point[0] >= global.stage.width || point[1] >= global.stage.height)
            return;
        this._colorBusy = true;
        const screenshot = new Shell.Screenshot();
        screenshot.pick_color(point[0], point[1], (object, result) => {
            this._colorBusy = false;
            if (!this.actor?.visible || this._appearance !== 'auto')
                return;
            try {
                const [ok, color] = object.pick_color_finish(result);
                if (!ok)
                    throw new Error('No screen color');
                const hex = '#' + [color.red, color.green, color.blue].map(value => value.toString(16).padStart(2, '0')).join('');
                ++this._colorSamples;
                const theme = this._paletteChoice.sample(hex, GLib.get_monotonic_time() / 1000);
                if (theme !== this._resolvedAppearance)
                    this.applyAppearance(theme);
            } catch (error) {
                if (this.diagnostics)
                    console.error('Color sampling: ' + error.stack);
                this.stopColorSampling();
                this.colorDetails.set_text(_('Screen colors are unavailable. The opaque reader keeps its contrast.'));
            }
        });
    }

    applyAppearance(theme) {
        this._resolvedAppearance = theme;
        const palette = T.palette[theme];
        for (const [role, key] of Object.entries(ROLES))
            C[role] = palette[key];
        // Inline styles were written with the default palette; swap their colors.
        const replace = new Map(Object.entries(ROLES).filter(([role]) => role !== 'focus')
            .map(([role, key]) => [T.color[role].toLowerCase(), palette[key]]));
        const visit = actor => {
            if (actor.get_style) {
                if (!this._originalStyles.has(actor))
                    this._originalStyles.set(actor, actor.get_style() || '');
                actor.set_style(this._originalStyles.get(actor).replace(/#[0-9a-f]{6}/gi, value => replace.get(value.toLowerCase()) || value));
            }
            for (const child of actor.get_children?.() || [])
                visit(child);
        };
        visit(this.actor);
        this.editor.set_style(`background-color:${palette.surface};color:${palette.text};border:2px solid ${palette.border};` +
            `border-radius:${N.buttonRadius}px;padding:${N.gap}px;font-size:${N.bodySize}pt;min-height:${N.padding * 4}px;`);
        this.actor.style_class = 'tp-' + theme;
        this.render();
    }

    setMirror(mirror) {
        this._mirror = mirror;
        // Beam-splitter rigs show the reader through a mirror.
        this.reader.scale_x = mirror === 'horizontal' ? -1 : 1;
        this.reader.scale_y = mirror === 'vertical' ? -1 : 1;
        this.state?.update({mirror});
        this.render();
    }

    resize(delta) {
        this._readerSize = Math.max(18, Math.min(64, this._readerSize + delta));
        this.reader.set_font_name(`${this._fontFamily || 'Sans'} ${this._readerSize}`);
        // Line height as a Pango attribute; Clutter merges it with the reader's markup.
        const attributes = new Pango.AttrList();
        if (this._lineSpacing !== 100)
            attributes.insert(Pango.attr_line_height_new(this._lineSpacing / 100));
        this.reader.set_attributes(attributes);
        this.state?.update({readerSize: this._readerSize});
        this.place();
    }

    setFont(family) {
        this._fontFamily = family;
        this.state?.update({fontFamily: family});
        this.resize(0);
        this.render();
    }

    setLineSpacing(percent) {
        this._lineSpacing = percent;
        this.state?.update({lineSpacing: percent});
        this.resize(0);
        this.render();
    }

    seek(position) {
        this.tracker.seek(position);
        this.render();
    }

    jumpSection(direction) {
        const target = this.tracker.sectionTarget(direction);
        if (target >= 0)
            this.tracker.seekCharacter(target);
        this.render();
    }

    changeSpeed(delta) {
        this._speed = Math.max(40, Math.min(400, this._speed + delta));
        this.state?.update({speed: this._speed});
        if (this._steadyTimer)
            this._runSteady(); // A new speed applies at once, without another countdown.
        this.render();
    }

    primaryAction() {
        if (this._mode === 'manual')
            this.seek(this.tracker.position + 4);
        else if (this._mode === 'steady')
            this._steadyTimer || this._countdownTimer ? this.stopSteady() : this.startSteady();
        else if (this._active || this.local.pending || this.local.owned)
            this.stop();
        else
            this.start();
        this.render();
    }

    startSteady() {
        this.stopSteady();
        if (this.tracker.position >= this.tracker.words.length - 1)
            this.tracker.seek(-1);
        this._countdownLeft = this._countdown;
        if (!this._countdownLeft) {
            this._runSteady();
            return;
        }
        this._countdownTimer = GLib.timeout_add_seconds(GLib.PRIORITY_DEFAULT, 1, () => {
            this._countdownLeft -= 1;
            if (this._countdownLeft > 0) {
                this.render();
                return GLib.SOURCE_CONTINUE;
            }
            this._countdownTimer = 0;
            this._runSteady();
            this.render();
            return GLib.SOURCE_REMOVE;
        });
    }

    _runSteady() {
        if (this._steadyTimer)
            GLib.source_remove(this._steadyTimer);
        this._steadyTimer = GLib.timeout_add(GLib.PRIORITY_DEFAULT, Math.max(150, Math.round(60000 / this._speed)), () => {
            if (this.tracker.position >= this.tracker.words.length - 1) {
                this._steadyTimer = 0;
                this.render();
                return GLib.SOURCE_REMOVE;
            }
            this.seek(this.tracker.position + 1);
            return GLib.SOURCE_CONTINUE;
        });
    }

    stopSteady() {
        if (this._steadyTimer)
            GLib.source_remove(this._steadyTimer);
        if (this._countdownTimer)
            GLib.source_remove(this._countdownTimer);
        this._steadyTimer = this._countdownTimer = this._countdownLeft = 0;
    }

    mode(mode) {
        this.stop();
        this._mode = mode;
        this.state?.update({mode});
        if (mode === 'voice')
            this.loadSettings();
        this.render();
    }

    showPane(pane) {
        const previous = this._pane;
        this._pane = pane;
        this.render();
        if (pane === 'settings')
            this.loadSettings();
        if (pane === 'edit')
            this.editor.grab_key_focus();
        if (!pane)
            (previous === 'edit' ? this.editButton : this.settingsButton).grab_key_focus();
    }

    toggle() {
        if (this.actor.visible) {
            this.hide();
            return;
        }
        this.actor.show();
        this.place();
        if (this._appearance === 'auto')
            this.appearance('auto');
        this.primary.grab_key_focus();
    }

    hide() {
        this.stopColorSampling();
        this.stop();
        this._pane = '';
        this.actor.hide();
        this.indicator.grab_key_focus();
    }

    async loadSettings() {
        try {
            const settings = await this.local.settings();
            if (this.actor)
                this.showSettings(settings);
        } catch {
            if (this.actor) {
                this._status = _('Local speech is not installed. Open Local speech setup to install a companion and language model.');
                this.render();
            }
        }
    }

    showSettings(settings) {
        this._backend = settings.selected;
        const dependencies = settings.dependencies;
        this._status = dependencies.ready
            ? _('%s · %s · Ready').format(this._backend === 'moonshine' ? 'Moonshine' : 'Vosk', dependencies.language || _('configured language'))
            : dependencies.missing.join(' ');
        this.render();
    }

    async chooseEngine(id) {
        if (this._active || this.local.pending || this.local.owned)
            return;
        try {
            const request = this.local.select(id);
            this.render();
            const settings = await request;
            if (this.actor && settings)
                this.showSettings(settings);
        } catch (error) {
            if (this.actor)
                this._status = String(error.message);
        } finally {
            if (this.actor)
                this.render();
        }
    }

    _stateText(listening, pending) {
        const end = this.tracker.words.length && this.tracker.position === this.tracker.words.length - 1;
        if (this._mode === 'manual')
            return end ? _('End of script · Microphone off') : _('Manual · Microphone off');
        if (this._mode === 'steady' && this._countdownTimer)
            return ngettext('Starting in %1 second…', 'Starting in %1 seconds…', this._countdownLeft).replace('%1', this._countdownLeft);
        if (this._mode === 'steady')
            return this._steadyTimer ? _('Scrolling · Microphone off') : end ? _('End of script · Microphone off') : _('Paused · Microphone off');
        if (pending)
            return _('Starting local speech…');
        if (listening)
            return this.tracker.lost ? _('Off script · Kalipso catches up when you return to the text') : _('Listening · On this device');
        return this._speechState === 'error' ? _('Speech unavailable · Open Settings') : _('Paused · Microphone off');
    }

    render() {
        if (!this.actor)
            return;
        const scale = this.desktopSettings?.get_double('text-scaling-factor') ?? 1;
        this.reader.set_markup(this.tracker.words.length
            ? this.tracker.readingMarkup(C.overlayMuted, C.overlayText, scale >= 1.8 ? -1 : scale >= 1.5 ? 0 : 4)
            : `<span foreground="${C.overlayMuted}">Add your script to begin.</span>`);
        this.counter.set_text(`${this.tracker.position + 1} / ${this.tracker.words.length}`);
        const words = this.tracker.words.length;
        this.progressFill.width = Math.round(this.mainContent.width * (words ? (this.tracker.position + 1) / words : 0));
        const firstLine = this.tracker.text.split('\n').map(line => line.replace(/^\s*#+\s*/, '').trim()).find(line => line);
        this.title.set_text(firstLine || 'Kalipso');
        const listening = this._active || this.local.owned, pending = this.local.pending;
        const hasWords = Boolean(this.tracker.words.length);
        this.primary.label = this._mode === 'manual' ? _('Next words')
            : this._mode === 'steady' ? (this._steadyTimer || this._countdownTimer ? _('Pause') : _('Start scrolling'))
            : listening ? _('Pause') : pending ? _('Cancel') : _('Start listening');
        const setEnabled = (button, enabled) => {
            button.reactive = enabled;
            button.can_focus = enabled;
        };
        setEnabled(this.primary, hasWords && (this._mode !== 'manual' || this.tracker.position < this.tracker.words.length - 1));
        setEnabled(this.previous, this.tracker.position >= 0);
        setEnabled(this.reset, this.tracker.position >= 0);
        const hasSections = sections(this.tracker.text).length > 0;
        this.previousSection.visible = hasSections;
        this.nextSection.visible = hasSections;
        this.speedRow.visible = this._mode === 'steady';
        this.speedLabel.set_text(_('%d words/min').format(this._speed));
        this.stateLabel.set_text(this._stateText(listening, pending));
        this.editPane.visible = this._pane === 'edit';
        this.settingsPane.visible = this._pane === 'settings';
        this.secondaryScroll.visible = Boolean(this._pane);
        this.editButton.label = this._pane === 'edit' ? _('Done editing') : _('Edit script');
        this.settingsButton.label = this._pane === 'settings' ? _('Done') : _('Settings');
        this.details.set_text(this._status);
        // The chosen option: accent border, tint and bold (:checked), and "checked" for assistive technology.
        const mark = (items, current) => {
            for (const [id, label, button] of items) {
                button.checked = id === current;
                button.accessible_name = label + (id === current ? ', selected' : '');
            }
        };
        mark(this.modeButtons, this._mode);
        mark(this.appearanceButtons, this._appearance);
        mark(this.mirrorButtons, this._mirror);
        mark(this.fontButtons, this._fontFamily);
        mark(this.spacingButtons, this._lineSpacing);
        mark(this.countdownButtons, this._countdown);
        this.countdownLabel.visible = this._countdownTimer !== 0;
        this.countdownLabel.text = String(this._countdownLeft);
        for (const [id, button] of this.engineButtons) {
            button.checked = id === this._backend;
            setEnabled(button, !listening && !pending);
        }
        this.state?.update({position: this.tracker.position >= 0 ? this.tracker.words[this.tracker.position].start : -1});
        this.place();
        if (this.diagnostics)
            console.log('KALIPSO_TEST_STATE ' + JSON.stringify({position: this.tracker.position, status: this._status}));
    }

    start() {
        if (this.local.pending || this.local.owned)
            return;
        this._lastSequence = -1;
        this.local.start().finally(() => this.render());
        this.render();
    }

    stop() {
        this.stopSteady();
        this.local?.stop();
        this._active = false;
        this._speechState = 'idle';
        this.render();
    }
}
