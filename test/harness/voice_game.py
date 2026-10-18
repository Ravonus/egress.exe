"""Actual fal PCM in both ROM mappers; an input-driven fall triggers speech while play continues."""
import argparse,hashlib,json,wave
from pathlib import Path
import numpy as np
from void_game import fixture

def run(rom,out):
    out.mkdir(parents=True,exist_ok=True);r={};g=fixture(rom,audio=True)
    manifest=json.loads(Path('assets/narrator/voice/manifest.json').read_text())
    for clip in manifest['clips']:
        sym=g.sym['_voice_data_'+str(clip['index'])];bank,at=sym>>16,sym&65535
        raw=bytes(g.pb.memory[bank,at:at+clip['bytes']])
        r['clip_'+clip['id']+'_matches_baked_fal_audio']=hashlib.sha256(raw).hexdigest()==clip['pcm_sha256']
    g.audio=[];hp=g.peek('pl_hp');quarter=g.peek('pl_quarter');before=hp*2-quarter;song=g.peek('music_song');g.hold({'right'});active=0;seq=g.peek('voice_seq');started=False;events=[];heights=[];fell=False
    fc=g.peek16('frame_count')
    for i in range(360):
        g.tick();heights.append(g.peek('pl_z'))
        if g.peek('pl_state')==6:fell=True;g.hold(set())
        if g.peek('voice_busy'):
            active+=1;started=True;events.append(g.peek('voice_clip'))
        if started and not g.peek('voice_busy'):break
    after=g.peek('pl_hp')*2-g.peek('pl_quarter');after_seq=g.peek('voice_seq')
    r['real_fall_triggers_fall_voice']=fell and started and set(events)=={1} and after_seq==seq+1
    r['fall_charges_exactly_one_quarter_heart']=after==max(1,before-1)
    r['native_voice_runs_at_expected_rate']=abs(active-manifest['clips'][1]['duration']*59.7275)<3
    elapsed=(g.peek16('frame_count')-fc)&65535
    r['speech_does_not_pause_gameplay']=elapsed>=i-1
    r['music_sequence_continues']=g.peek('music_song')==song
    data=np.frombuffer(b''.join(g.audio),dtype=np.int8).astype(np.int16)*256
    with wave.open(str(out/'fall-with-narrator-native.wav'),'wb') as w:
        w.setnchannels(2);w.setsampwidth(2);w.setframerate(g.pb.sound.sample_rate);w.writeframes(data.astype('<i2').tobytes())
    g.tick(90);r['speech_finishes_and_restores_music_channel']=not g.peek('voice_busy') and not g.peek('voice_dirty')
    g.shot(out/'after-fall-comment.png');g.stop()
    report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'results':r,'speech_bytes':manifest['total_pcm_bytes'],'voice_active_frames':active,'game_frames':elapsed,'display_frames':i+1,'fall':{'seen':fell,'quarterHeartsBefore':before,'quarterHeartsAfter':after,'clipIds':sorted(set(events)),'voiceSequenceBefore':seq,'voiceSequenceAfter':after_seq}}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return all(r.values())
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path);a=p.parse_args();raise SystemExit(0 if run(a.rom,a.out) else 1)
