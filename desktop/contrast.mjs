// SPDX-License-Identifier: AGPL-3.0-or-later
export function luminance(hex) {
    if(!/^#[0-9a-f]{6}$/i.test(hex))throw new Error('An opaque RGB color is required');
    const linear=value=>value<=0.04045?value/12.92:((value+0.055)/1.055)**2.4;
    const rgb=[1,3,5].map(offset=>linear(parseInt(hex.slice(offset,offset+2),16)/255));
    return 0.2126*rgb[0]+0.7152*rgb[1]+0.0722*rgb[2];
}
export function contrastRatio(a,b){const x=luminance(a),y=luminance(b);return (Math.max(x,y)+0.05)/(Math.min(x,y)+0.05);}
export function suggestedTheme(background,current='dark'){
    const level=luminance(background);
    return level>0.35?'dark':level<0.15?'light':current;
}
export class PaletteChoice {
    constructor(theme='dark',hold=2500){this.theme=theme;this.hold=hold;this.changedAt=-Infinity;}
    sample(background,now){const next=suggestedTheme(background,this.theme);if(next!==this.theme&&now-this.changedAt>=this.hold){this.theme=next;this.changedAt=now;}return this.theme;}
}
