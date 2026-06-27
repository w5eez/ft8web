// SPDX-License-Identifier: GPL-3.0-or-later

import { Injectable } from '@angular/core';
import { BehaviorSubject } from 'rxjs';
import { ApiService } from './api.service';
import { Radio } from './models';

@Injectable({ providedIn: 'root' })
export class RadioService {
  readonly radios$ = new BehaviorSubject<Radio[]>([]);
  readonly active$ = new BehaviorSubject<number>(0);

  constructor(private api: ApiService) { this.reload(); }

  reload(): void {
    this.api.getRadios().subscribe((r) => this.apply(r));
  }

  setActive(index: number): void {
    this.api.saveRadios({ active: index }).subscribe((r) => this.apply(r));
  }

  saveList(radios: Radio[], active?: number): void {
    this.api.saveRadios({ radios, active }).subscribe((r) => this.apply(r));
  }

  private apply(r: { radios: Radio[]; active: number }): void {
    this.radios$.next(r.radios);
    this.active$.next(r.active);
  }
}
