program ft2sim

! Generate an FT2 transmit WAV for WSJT-X Improved (DG2YCB).
! FT2 (jt9 --ft2, nmode=52) is decoded as FT4 run on 2x-upsampled audio
! (freq_ft2 = 2*freq_ft4, dt_ft2 = 0.5*dt_ft4 - 0.25) -- i.e. FT2 is FT4
! compressed 2x in time. 

  use wavhdr
  use packjt77
  include 'ft4_params.f90'               !NN, NSPS, NMAX (=79488, "sized for FT2 stretched")
  parameter (NTRP=45000)                 !FT2 T/R period = 3.75 s @ 12 kHz
  type(hdr) h
  character arg*12,fname*17,msg37*37,msgsent37*37,c77*77
  complex c0(0:NMAX-1)
  real wave(NMAX)
  integer itone(NN)
  integer*1 msgbits(77)
  integer*2 iout(NTRP)
  real f0,xdt

  nargs=iargc()
  if(nargs.lt.2) then
     print*,'Usage: ft2sim "message" f0 [dt fspread delay nfiles snr]'
     go to 999
  endif
  call getarg(1,msg37)
  call getarg(2,arg)
  read(arg,*) f0
  xdt=0.0
  if(nargs.ge.3) then
     call getarg(3,arg)
     read(arg,*) xdt
  endif

  fs=12000.0
  dt=1.0/fs

! FT4 source-encode -> itone, then synth at f0/2 (decimation doubles it back to f0).
  i3=-1
  n3=-1
  call pack77(msg37,i3,n3,c77)
  read(c77,'(77i1)') msgbits
  call genft4(msg37,0,msgsent37,msgbits,itone)
  icmplx=1
  call gen_ft4wave(itone,NN,NSPS,fs,f0/2.0,c0,wave,icmplx,NMAX)

! Position the FT4 signal.  dt_ft2 = 0.5*dt_ft4 - 0.25, so for FT2 dt = xdt use
! dt_ft4 = 0.5 + 2*xdt (mirrors ft4sim's k for that dt_ft4).
  k=nint((0.5+2.0*xdt+0.5)/dt)-NSPS
  c0=cshift(c0,-k)
  if(k.gt.0) c0(0:k-1)=0.0
  if(k.lt.0) c0(NMAX+k:NMAX-1)=0.0
  wave=real(c0)

! Decimate by 2 -> FT2 audio (NMAX/2 = 39744 samples), full-scale, pad to the period.
  datpk=maxval(abs(wave))
  if(datpk.le.0.0) datpk=1.0
  fac=32766.9/datpk
  iout=0
  do n=1,NMAX/2
     if(n.le.NTRP) iout(n)=nint(fac*wave(2*n-1))
  enddo

  h=default_header(12000,NTRP)
  fname='000000_000001.wav'
  open(10,file=fname,status='unknown',access='stream')
  write(10) h,iout
  close(10)
  write(*,'(a,a17,a,f8.2)') 'Wrote ',fname,'  f0=',f0

999 end program ft2sim
